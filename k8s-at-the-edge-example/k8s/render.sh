#!/bin/bash

# Array of node names
NODE_NAMES=("0198832759f82e89295467ae00000000")

# Create rendered directory if it doesn't exist
mkdir -p rendered

# Loop through each node
for node in "${NODE_NAMES[@]}"; do
    # Create node-specific directory
    mkdir -p "rendered/$node"
    
    # Process each template file
    for template in templates/*.y*ml; do
        # Get filename without path and extension
        filename=$(basename "$template")
        base="${filename%.*}"
        
        # Create rendered file in node directory
        NODE_NAME=$node envsubst '$NODE_NAME' < "$template" > "rendered/$node/${base}.yaml"
    done
    
    echo "Rendered templates for node: $node"
done

echo "All templates rendered successfully!"
