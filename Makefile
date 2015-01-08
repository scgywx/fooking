all:
	cd deps/lua/src && $(MAKE) a
	cd src && $(MAKE) $@
clean:
	cd deps/lua/src && $(MAKE) $@
	cd src && $(MAKE) $@