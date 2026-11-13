# Builds SottoAudio.driver (universal: arm64 + x86_64) on macOS with the Xcode command line tools.
#   make            build build/SottoAudio.driver (ad-hoc signed)
#   sudo make install   copy it to /Library/Audio/Plug-Ins/HAL and restart coreaudiod
#   sudo make uninstall
DRIVER = build/SottoAudio.driver
HAL = /Library/Audio/Plug-Ins/HAL

all: $(DRIVER)

$(DRIVER): SottoAudio.c Info.plist
	mkdir -p $(DRIVER)/Contents/MacOS
	cp Info.plist $(DRIVER)/Contents/Info.plist
	clang -arch arm64 -arch x86_64 -mmacosx-version-min=10.13 -O2 -Wall -Wextra -Wno-unused-parameter \
		-bundle -framework CoreAudio -framework CoreFoundation \
		-o $(DRIVER)/Contents/MacOS/SottoAudio SottoAudio.c
	codesign --force --sign - $(DRIVER)

install: $(DRIVER)
	rm -rf $(HAL)/SottoAudio.driver
	cp -R $(DRIVER) $(HAL)/
	killall -9 coreaudiod || true

uninstall:
	rm -rf $(HAL)/SottoAudio.driver
	killall -9 coreaudiod || true

clean:
	rm -rf build
