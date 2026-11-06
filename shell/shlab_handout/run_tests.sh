#!/bin/bash

# Loop from 1 to 17, formatting numbers with a leading zero (e.g., 01, 02... 17)
for i in $(seq -w 01 17); do
    make "test$i"
    make "rtest$i"
done