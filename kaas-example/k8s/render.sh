#!/bin/bash

# Function to display help
show_help() {
    cat << EOF
Usage: $0 [OPTIONS] <node_name1> [node_name2] ...

Render Kubernetes templates for specified nodes.

ARGUMENTS:
    node_name    Node name(s) in UUID format (32 hex characters without hyphens)
                Example: 550e8400e29b41d4a716446655440000

OPTIONS:
    -h, --help   Show this help message and exit

EXAMPLES:
    $0 550e8400e29b41d4a716446655440000
    $0 550e8400e29b41d4a716446655440000 6ba7b8109dad11d180b400c04fd430c8

DESCRIPTION:
    This script renders Kubernetes template files for the specified node(s).
    Each node name must be a valid UUID format (32 hex characters without hyphens).
    Templates are processed from the templates/ directory and rendered to
    the rendered/<node_name>/ directory.

EOF
}

# Function to validate UUID format
validate_uuid() {
    local uuid="$1"
    # UUID regex pattern: 32 hexadecimal digits without hyphens
    local uuid_pattern='^[0-9a-fA-F]{32}$'
    
    if [[ $uuid =~ $uuid_pattern ]]; then
        return 0
    else
        return 1
    fi
}

# Check for help option
if [[ "$1" == "-h" || "$1" == "--help" ]]; then
    show_help
    exit 0
fi

# Check if at least one argument is provided
if [[ $# -eq 0 ]]; then
    echo "Error: No node names provided."
    echo "Use '$0 --help' for usage information."
    exit 1
fi

# Validate all provided node names are UUIDs
for node in "$@"; do
    if ! validate_uuid "$node"; then
        echo "Error: '$node' is not a valid UUID format."
        echo "Node names must be in UUID format (32 hex characters without hyphens)"
        echo "Example: 550e8400e29b41d4a716446655440000"
        echo "Use '$0 --help' for usage information."
        exit 1
    fi
done

# Array of node names
NODE_NAMES=("$@")

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
