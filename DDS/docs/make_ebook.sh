#! /bin/bash
pandoc -o output.epub --wrap=preserve --syntax-highlighting=none --css=epub-style.css metadata.yaml $(ls -v *.md)
