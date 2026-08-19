#!/usr/bin/env ruby

require "json"
require "yaml"

source_dir = ARGV.fetch(0, File.join(__dir__, "..", "data", "starman", "yaml"))

def load_legacy_json(path)
  # M5AvatarLiteServo.json has a legacy trailing comma.
  source = File.read(path).gsub(/,\s*([}\]])/, '\\1')
  JSON.parse(source)
end

def deep_copy(value)
  Marshal.load(Marshal.dump(value))
end

def fix_legacy_paths(value, basename)
  case value
  when Hash
    value.each { |key, child| value[key] = fix_legacy_paths(child, basename) }
  when Array
    value.map! { |child| fix_legacy_paths(child, basename) }
  when String
    value = value.gsub("eye__r", "eye_r")
    value = value.sub(%r{\A/bmp/}, "/bmp_starman/") if basename == "M5AvatarLite_starman"
  end
  value
end

def find_expression_part(entries, expression)
  entries ||= []
  match = entries.find { |entry| entry.key?(expression) }
  match ||= entries.find { |entry| entry.key?("normal") }
  match ||= entries.first
  match ? deep_copy(match.values.first) : {}
end

def normalize_avatar(document, basename)
  # The application and M5StackImageAvatarLib's 4 KB parser use these three
  # fixed expression indexes. Missing artwork falls back to normal below.
  expressions = ["normal", "sad", "angry"]
  document["expression"] = expressions

  document["mouth"] = expressions.map do |expression|
    { expression => find_expression_part(document["mouth"], expression) }
  end

  if document["eye"]
    document["eye"] = expressions.map do |expression|
      { expression => find_expression_part(document["eye"], expression) }
    end
  else
    document["eye"] = expressions.map do |expression|
      right = find_expression_part(document["eye_r"], expression)
      left = find_expression_part(document["eye_l"], expression)
      {
        expression => {
          "rx" => right["x"], "ry" => right["y"],
          "lx" => left["x"], "ly" => left["y"],
          "w" => right["w"], "h" => right["h"],
          "filename" => right["filename"],
          "minScaleX" => right["minScaleX"], "maxScaleX" => right["maxScaleX"],
          "minScaleY" => right["minScaleY"], "maxScaleY" => right["maxScaleY"],
          "invert" => true
        }
      }
    end
    document.delete("eye_r")
    document.delete("eye_l")
  end

  document["init_param"] = expressions.map do |expression|
    { expression => find_expression_part(document["init_param"], expression) }
  end
  document.fetch("color_info", {}).delete("transparent2")
  fix_legacy_paths(document, basename)
end

documents = {}
Dir.glob(File.join(source_dir, "*.json")).sort.each do |path|
  documents[File.basename(path, ".json")] = load_legacy_json(path)
end

avatar_names = documents.select { |_name, document| document.key?("expression") }.keys

documents.each do |basename, document|
  if document.key?("expression")
    normalize_avatar(document, basename)
  elsif document.key?("avatar_json")
    configured = document.delete("avatar_json").map do |path|
      File.basename(path, ".json")
    end.select { |name| avatar_names.include?(name) }
    configured.concat(avatar_names) if basename == "M5AvatarLiteSystem"
    document["avatar_yaml"] = configured.uniq.map { |name| "/yaml/#{name}.yaml" }
    servo_path = document.delete("servo_json")
    document["servo_yaml"] = "/yaml/#{File.basename(servo_path, ".json")}.yaml" if servo_path
    document["audio_led_lr"] = document.delete("led_lr") || 0
    document["use_takao_base"] = false unless document.key?("use_takao_base")
    document["mic_swing_interval"] = 0 unless document.key?("mic_swing_interval")
  end

  output_path = File.join(source_dir, "#{basename}.yaml")
  File.write(output_path, YAML.dump(document))
  puts "generated #{output_path}"
end
