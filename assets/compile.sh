#!/usr/bin/env bash

SASS="sass"
STYLESHEETS=("handy-window" "handy-window-dark")

if ! command -v $SASS >/dev/null
then
  echo  -e "🙅 Please install sass"
  exit 1
fi

for STYLESHEET in "${STYLESHEETS[@]}"
do
  echo "Compile $STYLESHEET.scss"
  $SASS "$STYLESHEET.scss" "$STYLESHEET.css" --style=compressed --no-source-map
done

echo "Done"
