# source scripts/env.sh
if [ -x /opt/homebrew/bin/brew ]; then
  eval "$(/opt/homebrew/bin/brew shellenv)"
fi
if [ -d /opt/homebrew/opt/make/libexec/gnubin ]; then
  export PATH="/opt/homebrew/opt/make/libexec/gnubin:$PATH"
fi
