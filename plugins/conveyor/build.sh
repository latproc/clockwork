#!/bin/sh

if [ "$1" = "-l" ]; then
ctangle controller.w \
	&& cweave controller.w \
	&& sed -i 's/\\input cwebmac/\\input eplain\\input cwebmac/' controller.tex \
	&& pdftex -halt-on-error controller.tex
else
ctangle controller.w \
	&& cweave controller.w \
	&& pdftex -halt-on-error controller.tex
fi
touch .lastrun
