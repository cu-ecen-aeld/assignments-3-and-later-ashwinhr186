#!/bin/bash

#Author: Ashwin Ravindra
#This script was written with the help of chatGPT 

# Validate whether the script is run with 2 arguments
if [ "$#" -ne 2 ]; 
then
  echo "Error: Invalid number of arguments !"
  exit 1
fi

filesdir="$1"
searchstr="$2"

# Validate whether the first argument is a directory
if [ ! -d "$filesdir" ]; 
then
  echo "Error: '$filesdir' is not a directory or does not exist."
  exit 1
fi

# Initialize counters
num_of_files=0
num_of_occurrences=0

# Function to search for lines containing searchstr in a file
search() {
  local file="$1"
  local search="$2"
  local count=$(grep -c "$search" "$file")
  num_of_occurrences=$((num_of_occurrences + count))
}

# functiion to traverse files and directories under $filesdir directory recursively
traverse() {
  local dir="$1"
  for item in "$dir"/*; 
  do
    #if item is a file, search for the string
    if [ -f "$item" ]; 
    then
      search "$item" "$searchstr"
      num_of_files=$((num_of_files + 1))
    #if item is a directory, traverse the directory
    elif [ -d "$item" ]; 
    then
      traverse "$item"
    fi
  done
}

# Initiate the traversal to $filestr directory
traverse "$filesdir"

echo "The number of files are $num_of_files and the number of matching lines are $num_of_occurrences"

exit 0
