TEMPLATE_VER := "v1.0.0"
TARGET := "D:\\Games\\GamePlatforms\\SteamLibrary\\steamapps\\common\\Left 4 Dead 2\\neko\\plugins"

# use `just preset=x64-release config build` to override this
preset := "x86-release-msvc"
target := "main"

[linux]
init:
    bash ./scripts/init_linux.sh

clean:
    rm -rf build

build:
    xmake f -m release
    xmake

# install the L4N plugin into <L4D2>/neko/plugins/
install: build
    cp build/windows/x86/release/necola_ads.dll "{{TARGET}}/"

release:
    rm release -rf
    mkdir -p release

    # release
    xmake f -m release
    xmake
    cp build/windows/x86/release/necola_ads.dll release/
    cp kpatch.ini                                   release/

@run:
    ./build/{{preset}}/bin/{{target}}
