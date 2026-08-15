# Virtual Device Server K8s Templates Renderer

This directory contains a script, `render.sh`, that automates the rendering of Kubernetes YAML templates for multiple edge nodes.

## What does `render.sh` do?

- Iterates over a list of Izuma's Device IDs.
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
   cd mbed-edge-examples/kaas-example/k8s
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

## Init container example

`templates/init-container-demo-pod.yaml` is a small, self-contained pod you can
deploy to check that **init containers** work on your edge node.

An init container runs to completion *before* the application container starts.
Both containers mount the same `emptyDir` volume, which is how the init
container hands its output to the application:

- `init-prepare-content` (init container) generates an HTML page into `/work`
- `web` (application container) is nginx, serving that same volume

Because the page can only have been produced by the init container, seeing it in
the response proves the init container ran, finished, and passed its work along.

It needs no Service, no DNS and no database, so it can be deployed on its own.

### Deploy it

```sh
./render.sh <device-id>
kubectl apply -f rendered/<device-id>/init-container-demo-pod.yaml
kubectl get pod init-demo-<device-id>
```

While the init container is running the pod shows `Init:0/1`; once it completes
the pod moves to `Running`.

### Check the result

From the edge node itself — the pod publishes `hostPort: 31080`:

```sh
curl http://localhost:31080/
```

Expected output:

```html
<h1>Init container demo</h1>
<p>device: 0197b36304082e89295467ae00000000</p>
<p>prepared by: init-prepare-content (init container)</p>
<p>prepared at: 2026-01-01T00:00:00Z</p>
```

The init container's own log is available with:

```sh
kubectl logs init-demo-<device-id> -c init-prepare-content
```

If the pod never starts and reports `Predicate PodFitsHostPorts failed`, another
pod on that node is already using port 31080. Find it with:

```sh
kubectl get pods -o wide --field-selector spec.nodeName=<device-id>
```

then either remove that pod or change `hostPort` in the template. The port is
only there to make the result easy to `curl`; the example does not need it, and
the `hostPort` line can be dropped entirely if you would rather check the result
with `kubectl logs`.

### Try it yourself

- Add a second entry under `initContainers` and redeploy. They run one after
  another, in the order listed, and the application container starts only once
  all of them have exited 0.
- Make an init container exit non-zero. The pod stays in `Init:Error` /
  `Init:CrashLoopBackOff` and the application container never starts. This
  gating is what makes init containers useful for preparing state, or waiting on
  a dependency, before an application runs.

### Clean up

```sh
kubectl delete pod init-demo-<device-id>
```

## DaemonSet example (pinned to one device)

`templates/daemonset-demo.yaml` deploys a **DaemonSet** — but restricted to a
single device, so a demo does not roll out across your whole fleet.

### Limiting a DaemonSet to one node

A DaemonSet normally places one pod on every node. Every Izuma edge node is
automatically labelled with its device ID, which you can see with:

```sh
kubectl get nodes --show-labels
```

```
NAME       STATUS  ...  LABELS
01a002...  Ready   ...  beta.kubernetes.io/arch=amd64,beta.kubernetes.io/os=linux,kubernetes.io/hostname=01a002...
```

So a `nodeSelector` on `kubernetes.io/hostname` targets exactly one device, with
no manual labelling required:

```yaml
      nodeSelector:
        kubernetes.io/hostname: <device-id>
```

To roll out to the whole fleet instead, remove the `nodeSelector` block (or set
it to `{}`). To target every node of one architecture, select on
`beta.kubernetes.io/arch` instead.

### Deploy it

```sh
./render.sh <device-id>
kubectl apply -f rendered/<device-id>/daemonset-demo.yaml
```

### Check the result

```sh
kubectl get daemonset daemonset-demo-<device-id>
```

```
NAME                      DESIRED   CURRENT   READY   UP-TO-DATE   AVAILABLE   NODE SELECTOR
daemonset-demo-<device>   1         1         1       1            1           kubernetes.io/hostname=<device-id>
```

`DESIRED 1` is the confirmation that the `nodeSelector` worked — without it that
column would show the number of Ready nodes in your fleet.

Confirm which node it actually landed on:

```sh
kubectl get pods -l app=daemonset-demo -o wide
```

The pod reports its own placement too, using the downward API. From the edge
node (the pod publishes `hostPort: 31081`):

```sh
curl http://localhost:31081/
```

```html
<h1>DaemonSet demo</h1>
<p>pod: daemonset-demo-<device-id>-x7k2p</p>
<p>node: <device-id></p>
<p>started: 2026-01-01T00:00:00Z</p>
```

### Rollout and rollback

DaemonSets support staged updates. This example sets `maxUnavailable: 1`, so
nodes update one at a time — if a bad image is rolled out, only one node is
affected while the rest keep running the previous version.

```sh
kubectl rollout status  daemonset daemonset-demo-<device-id>
kubectl rollout history daemonset daemonset-demo-<device-id>
kubectl rollout undo    daemonset daemonset-demo-<device-id>
kubectl rollout undo    daemonset daemonset-demo-<device-id> --to-revision=1
```

### Clean up

```sh
kubectl delete daemonset daemonset-demo-<device-id>
```
