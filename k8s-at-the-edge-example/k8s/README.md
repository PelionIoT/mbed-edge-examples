# Virtual Device Server K8s Templates Renderer

This directory contains a script, `render.sh`, that automates the rendering of Kubernetes YAML templates for multiple edge nodes.

## What does `render.sh` do?

- Iterates over a predefined list of Izuma's Device IDs.
- For each node, it:
  - Creates a directory under `rendered/` specific to that node.
  - Processes all template files in the `templates/` directory (files ending with `.yml` or `.yaml`).
  - Uses `envsubst` to substitute environment variables (notably `NODE_NAME`) in each template, producing a rendered YAML file for each node.
  - Places the rendered files in the corresponding `rendered/<node>/` directory.

## Prerequisites

- **Bash**: The script is written for bash.
- **envsubst**: This utility is required for variable substitution. It is usually available via the `gettext` package.
  - On macOS: `brew install gettext && brew link --force gettext`
  - On Ubuntu/Debian: `sudo apt-get install gettext`

## How to Run

1. **Navigate to this directory:**
   ```sh
   cd mbed-edge-examples/virtual-device-server/k8s
   ```
2. **Ensure your templates are in the `templates/` directory.**
   - Template files should use the `.yml` or `.yaml` extension.
   - Use the variable `$NODE_NAME` in your templates where you want the node name substituted.
3. **Run the script:**
   ```sh
   ./render.sh
   ```
   If you get a permission denied error, make the script executable:
   ```sh
   chmod +x render.sh
   ./render.sh
   ```

## Output

- Rendered YAML files will be placed in `rendered/<node>/` for each node in the script's `NODE_NAMES` array.
- Each file will have the same base name as the template, but with a `.yaml` extension.

## Customization

- To render for different nodes, edit the `NODE_NAMES` array at the top of `render.sh`.
- To add or modify templates, place your files in the `templates/` directory.

## Example

Suppose you have a template `templates/deployment.yaml` containing:

```yaml
apiVersion: v1
kind: Pod
metadata:
  name: device-$NODE_NAME
spec:
  ...
```

After running the script, you will get:
- `rendered/0197b36304082e89295467ae00000000/deployment.yaml`
- `rendered/0195936439b66240d0040fa600000000/deployment.yaml`

with `$NODE_NAME` replaced by the actual node name in each file. 