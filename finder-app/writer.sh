#!/bin/bash

#Author: Ashwin Ravindra
#This script was written with the help of chatGPT prompt

# Validate whether the script is run with 2 arguments
if [ "$#" -ne 2 ]; 
then
  echo "Error: Invalid number of arguments"
  exit 1
fi

writefile="$1"
writestr="$2"

# Create the directory path if it doesn't exist
mkdir -p "$(dirname "$writefile")"

# Check if the directory path is created successfully
if [ ! -d "$(dirname "$writefile")" ]; 
then
  echo "Error: Could not create directory '$(dirname "$writefile")'"
  exit 1
fi

# Write $writestr to $writefile
echo "$writestr" > "$writefile"

# Check if the file was created successfully
if [ ! -f "$writefile" ]; 
then
  echo "Error: Could not create or write to '$writefile'"
  exit 1
fi

echo "Content $writestr was written to $writefile successfully"

exit 0
