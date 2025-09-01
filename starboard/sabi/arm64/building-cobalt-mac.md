brew install bison ccache node pre-commit
export PATH=“/opt/homebrew/opt/bison/bin:$PATH”
which bison
export PATH=“/Users/abhijeet/code/chromium/src/buildtools/mac:$PATH”
which gn
sudo xcode-select -s ~/Documents/Xcode.app/Contents/Developer
xcodebuild -version
Xcode 16.1
Build version 16B40

git clone https://gitlab.igalia.com/p/youtube-tvos/cobalt.git

git checkout -b igalia/25.lts.1±tvos-build-fix origin/igalia/25.lts.1±tvos-build-fix

cobalt % export PYTHONPATH=$PWD:$PYTHONPATH

cobalt % cd starboard

starboard % git clone https://gitlab.igalia.com/p/youtube-tvos/keyboxes-dev.git keyboxes/

cobalt % pre-commit install -t post-checkout -t pre-commit -t pre-push --allow-missing-config

cobalt % pre-commit run --verbose
[WARNING] hook id check-copyright-year uses deprecated stage names (push) which will be removed in a future version. run: pre-commit migrate-config to automatically fix this.

cobalt % pre-commit migrate-config

cobalt % git add .pre-commit-config.yaml

cobalt % pre-commit run --verbose
Some checked are Passed and some of them are Skipped. There are also lot of WARNING messages

cobalt % ./cobalt/build/gn.py -c debug -p darwin-tvos-simulator

cobalt % ninja -C out/darwin-tvos-simulator_debug cobalt
if you get an error : FileNotFoundError: [Errno 2] No such file or directory: ‘nasm’

cobalt % brew install nasm

cobalt % which nasm && nasm -v

cobalt % export PATH="/opt/homebrew/bin:$PATH"

cobalt % ninja -C out/darwin-tvos-simulator_debug cobalt

cobalt % /Users/abhijeet/code/chromium/src/out/Debug-appletvsimulator/iossim -x tvos -v -d 'Apple TV 4K (3rd generation)' -i out/darwin-tvos-simulator_debug/install/cobalt.app/

Run : open -a Simulator.app in another terminal window.

For debugging :

cobalt % echo $PATH
/opt/homebrew/bin:/Users/abhijeet/code/chromium/src/buildtools/mac:/Users/abhijeet/code/chromium/src/buildtools/mac/gn:/opt/homebrew/opt/bison/bin:/opt/homebrew/opt/node@20/bin:/opt/homebrew/bin:/opt/homebrew/sbin:/opt/homebrew/opt/ccache/libexec:/usr/local/bin:/System/Cryptexes/App/usr/bin:/usr/bin:/bin:/usr/sbin:/sbin:/var/run/com.apple.security.cryptexd/codex.system/bootstrap/usr/local/bin:/var/run/com.apple.security.cryptexd/codex.system/bootstrap/usr/bin:/var/run/com.apple.security.cryptexd/codex.system/bootstrap/usr/appleinternal/bin:/Library/Apple/usr/bin:/Users/abhijeet/code/depot_tools