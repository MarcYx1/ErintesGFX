# TouchGFX textconvert still calls File.exists?/Dir.exists?, removed in Ruby 3.2.
# Loaded through RUBYOPT so the ST scripts stay untouched.
[File, Dir].each do |k|
  k.singleton_class.send(:alias_method, :exists?, :exist?) unless k.respond_to?(:exists?)
end
