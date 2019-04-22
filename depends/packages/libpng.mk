package=libpng
$(package)_version=1.6.34
$(package)_download_path=http://ftp.osuosl.org/pub/libpng/src/libpng16/
$(package)_file_name=$(package)-$($(package)_version).tar.gz
$(package)_sha256_hash=574623a4901a9969080ab4a2df9437026c8a87150dfd5c235e28c94b212964a7
$(package)_dependencies=zlib

define $(package)_set_vars
  $(package)_config_opts=--enable-static --disable-shared
endef

define $(package)_config_cmds
  $($(package)_autoconf)
endef

define $(package)_build_cmds
  $(MAKE) $($(package)_build_opts) PNG_COPTS='-fPIC'
endef

define $(package)_stage_cmds
  $(MAKE) DESTDIR=$($(package)_staging_dir) install $($(package)_build_opts)
endef

