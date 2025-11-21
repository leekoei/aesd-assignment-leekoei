#!/bin/bash

# Check if two arguments are provided
if [ "$#" -ne 2 ]; then
    echo "Error: exactly two arguments are required."
    echo "Usage: $0 <path-to-file> <text_string>"
    exit 1
fi

writefile=$1
writestr=$2

# Create the file writefile
if ! mkdir -p "$(dirname "$writefile")" && touch "$writefile" ; then
    echo "Error: could not create '$writefile'."
    exit 1
fi

# Update writefile with writestr
echo "$writestr" > "$writefile"

