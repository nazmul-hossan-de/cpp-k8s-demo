# C++ Kubernetes Demo

An end-to-end, learning-focused C++ microservice demonstration covering local compilation, static analysis, Debian packaging, Docker images, Kubernetes, Helm, Vault secret injection, and GitHub Actions automation.

The application is intentionally small: it can read an API key from a file when `API_KEY_FILE` is set, then emits a heartbeat every five seconds. It never logs the key value.

## Summary

This repository demonstrates a C++ application lifecycle, from native compilation and packaging through local Kubernetes deployment and secret injection. The Vault dev-mode setup is for learning only and is not production-ready.

The repository demonstrates these boundaries:

- **Source:** `src/main.cpp`
- **Native build:** CMake and Ninja
- **Quality checks:** Cppcheck and Clang-Tidy
- **Debian packaging:** metadata in `packaging/debian/`, copied to `debian/` by CI before `dpkg-buildpackage`
- **Container packaging:** multi-stage Docker build
- **Kubernetes manifests:** `k8s/deployment.yaml` and `k8s/web.yaml`
- **Helm chart:** `charts/cpp-k8s-demo/`
- **Vault integration:** Vault Agent Injector and Kubernetes ServiceAccount authentication
- **Automation:** `.github/workflows/ci.yml`

## Step-by-Step Learning Journey

### Step 1: Environment & Toolchain Setup

#### Environment

The original development environment was WSL 2 running Ubuntu 24.04. Install the core native toolchain with:

```bash
sudo apt-get update
sudo apt-get install -y build-essential cmake ninja-build
```

The project expects a C++17-capable compiler. The build uses CMake to generate Ninja files and produces the `cpp_app` executable.

#### Build Architecture

The source and build configuration are intentionally separated:

```text
cpp-k8s-demo/
├── CMakeLists.txt
├── src/
│   └── main.cpp
├── packaging/
│   └── debian/
├── charts/
│   └── cpp-k8s-demo/
│       ├── Chart.yaml
│       ├── values.yaml
│       └── templates/
│           └── deployment.yaml
├── k8s/
│   ├── deployment.yaml
│   └── web.yaml
├── Dockerfile
└── .github/
    └── workflows/
        └── ci.yml
```

Configure and compile locally:

```bash
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
./build/cpp_app
```

The program runs continuously and prints a heartbeat every five seconds. Stop it with `Ctrl+C`.

#### Tools Used

- **WSL 2:** Runs Ubuntu 24.04 alongside Windows and provides a Linux development environment.
- **Ubuntu:** Supplies the package manager, compiler libraries, shell tools, and runtime environment.
- **g++:** Compiles the C++17 source code into the native `cpp_app` executable.
- **CMake:** Describes the project and generates platform-specific build files.
- **Ninja:** Executes the generated build graph quickly and incrementally.

### Step 2: Static Analysis & Quality Control

#### Cppcheck

Cppcheck scans the source for common memory, correctness, and portability problems:

```bash
cppcheck \
  --enable=all \
  --suppress=missingIncludeSystem \
  --error-exitcode=1 \
  src/
```

The `missingIncludeSystem` suppression avoids treating unavailable system-header metadata as a project failure in the CI environment. Cppcheck still returns a nonzero status for findings that matter to the build.

#### Clang-Tidy

`.clang-tidy` enables checks across:

- `bugprone-*`
- `clang-analyzer-*`
- `performance-*`
- `readability-*`
- `modernize-*`

Warnings are treated as errors. After configuring CMake with compile commands enabled, run:

```bash
clang-tidy src/main.cpp -p build/
```

#### VS Code Integration

`.vscode/tasks.json` provides tasks for:

- **CMake: Build**
- **Static Analysis: Cppcheck**
- **Static Analysis: Clang-Tidy**

The build task is the default VS Code build task and can be launched with `Ctrl+Shift+B`.

#### Tools Used

- **Cppcheck:** Performs lightweight static analysis for correctness and common defect patterns.
- **Clang-Tidy:** Applies compiler-assisted checks for bugs, readability, performance, and modern C++ practices.
- **VS Code tasks:** Provides repeatable editor commands for building and analyzing the project.

### Step 3: Dual Packaging: Debian and Docker

The project produces both a native Debian package and a container image. These formats serve different delivery needs: the Debian package integrates with a Linux host, while the container packages the runtime for repeatable deployment.

#### Debian Package

The Debian metadata is kept under version control in `packaging/debian/`:

- `control` declares package metadata and build dependencies.
- `rules` delegates the package build to debhelper and CMake.
- `changelog` records the package version and release history.

Build the package locally:

```bash
sudo apt-get install -y debhelper debhelper-compat
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
cp -r packaging/debian debian
dpkg-buildpackage -us -uc -b
```

`dpkg-buildpackage` writes the resulting `.deb` file to the parent directory. The GitHub Actions workflow copies that package into `artifacts/` before uploading it, because `actions/upload-artifact@v4` does not accept paths outside the workspace.

The generated root-level `debian/` directory and `.deb` files are ignored by Git. The source metadata under `packaging/debian/` remains tracked.

#### Docker Multi-Stage Build

The `Dockerfile` has two stages:

1. **Builder:** `ubuntu:24.04` with CMake, Ninja, and g++, which compiles the application.
2. **Runner:** a clean `ubuntu:24.04` image containing only the runtime library and compiled executable.

Build and run the image:

```bash
docker build -t cpp-k8s-demo:ci .
docker run --rm cpp-k8s-demo:ci
```

The application is designed to run continuously. Stop the local container with `Ctrl+C`, or use a bounded smoke test:

```bash
docker run -d --name cpp-k8s-demo-ci cpp-k8s-demo:ci
timeout 10s docker logs -f cpp-k8s-demo-ci || test $? -eq 124
docker rm -f cpp-k8s-demo-ci
```

#### Key Container Insights

- The builder and runner use the same Ubuntu release, preserving glibc compatibility between compilation and execution.
- Build tools stay out of the final runtime image.
- `.dockerignore` excludes `build/`, `.git/`, `.vscode/`, `debian/`, and generated `.deb` files.
- Excluding host build output prevents stale `CMakeCache.txt` files and host-specific paths from entering the Docker build context.

#### Tools Used

- **dpkg-buildpackage:** Builds the native Debian package from the Debian metadata and compiled project.
- **debhelper:** Provides standard Debian packaging helpers and invokes the CMake build system through `debian/rules`.
- **Docker:** Builds and runs the application in an isolated, reproducible environment.
- **Dockerfile:** Defines the builder and runtime image stages.
- **.dockerignore:** Removes unnecessary files from the Docker build context.

### Step 4: Kubernetes Basics and Kind

Kind creates a local Kubernetes cluster using Docker containers as nodes. A node runs Pods; a Pod contains one or more containers. A Deployment declares the desired number of Pod replicas and replaces Pods that stop. Namespaces group resources inside a cluster; they do not create separate clusters.

Create the local cluster and inspect its node:

```bash
kind create cluster --name dev-cluster1
kubectl get nodes
kubectl get namespaces
```

Create an application namespace:

```bash
kubectl create namespace cpp-app --dry-run=client -o yaml | kubectl apply -f -
```

The `k8s/web.yaml` example contains an Nginx Deployment and a ClusterIP Service. It can be applied independently to the `default` namespace:

```bash
kubectl apply -f k8s/web.yaml
kubectl get deployments,services,pods
kubectl delete -f k8s/web.yaml
```

Build the C++ image and load it into the Kind node. This avoids publishing it to a registry during local development:

```bash
docker build -t cpp-k8s-demo:v1 .
kind load docker-image cpp-k8s-demo:v1 --name dev-cluster1
```

The C++ Deployment manifest is configured for the `cpp-app` namespace and, in Step 6, will use Vault Agent injection. Do not apply it until Vault, its Kubernetes auth role, and the secret are configured.

Useful inspection commands include:

```bash
kubectl get deployments,pods -n cpp-app
kubectl get pods -A
kubectl describe pod <pod-name> -n cpp-app
kubectl logs <pod-name> -n cpp-app
```

`kubectl get pods -A` lists Pods across namespaces. Pod IPs are internal and can change when a Pod is replaced; a Service provides a stable network endpoint for applications that listen for network traffic.

#### Tools Used

- **Kind:** Creates a local Kubernetes cluster with containerized nodes.
- **kubectl:** Applies manifests and inspects cluster resources.
- **Deployment:** Maintains the requested number of application Pods.
- **Service:** Provides a stable endpoint for selected Pods.

### Step 5: Helm Chart

The chart under `charts/cpp-k8s-demo/` turns the C++ Deployment into a reusable package. `Chart.yaml` identifies the chart, `values.yaml` contains configurable defaults, and `templates/deployment.yaml` renders a Kubernetes Deployment from those values.

Validate and preview the chart, then install it as a Helm release:

```bash
helm lint ./charts/cpp-k8s-demo
helm template cpp-k8s-demo-helm ./charts/cpp-k8s-demo -n cpp-app
helm upgrade --install cpp-k8s-demo-helm ./charts/cpp-k8s-demo -n cpp-app
helm list -n cpp-app
kubectl get deployments,pods -n cpp-app
```

`helm template` prints rendered YAML without changing the cluster. `helm upgrade --install` creates or updates the named release. The chart release creates a separate Deployment from the one managed directly by `k8s/deployment.yaml`, so installing both results in additional Pods. Remove the example release with:

```bash
helm uninstall cpp-k8s-demo-helm -n cpp-app
```

### Step 6: Vault Secret Injection

This learning setup runs Vault in dev mode inside Kind. Dev mode uses an insecure root token and in-memory storage, so data is lost when Vault restarts. Use only fake secrets; never use this setup or token in production.

Install the HashiCorp chart and wait for Vault and the Agent Injector Pods:

```bash
helm repo add hashicorp https://helm.releases.hashicorp.com
helm repo update
helm upgrade --install vault hashicorp/vault \
  --namespace vault \
  --create-namespace \
  --set server.dev.enabled=true \
  --set server.dev.devRootToken=learning-only-root
kubectl get pods -n vault
```

Store a fake API key in Vault's KV engine:

```bash
kubectl exec -n vault vault-0 -- env VAULT_TOKEN=learning-only-root \
  vault kv put secret/cpp-app api-key=learning-only-fake-key
```

Create a policy that permits reading only this KV v2 secret:

```bash
kubectl exec -i -n vault vault-0 -- env VAULT_TOKEN=learning-only-root \
  vault policy write cpp-app-read - <<'EOF'
path "secret/data/cpp-app" {
  capabilities = ["read"]
}
EOF
```

Enable Kubernetes authentication and configure Vault to verify Pod ServiceAccount tokens through the Kubernetes API:

```bash
kubectl exec -n vault vault-0 -- env VAULT_TOKEN=learning-only-root \
  vault auth enable kubernetes
kubectl exec -n vault vault-0 -- env VAULT_TOKEN=learning-only-root sh -c \
  'vault write auth/kubernetes/config \
    kubernetes_host="https://${KUBERNETES_SERVICE_HOST}:${KUBERNETES_SERVICE_PORT}" \
    kubernetes_ca_cert=@/var/run/secrets/kubernetes.io/serviceaccount/ca.crt'
kubectl exec -n vault vault-0 -- env VAULT_TOKEN=learning-only-root \
  vault write auth/kubernetes/role/cpp-app \
    bound_service_account_names=cpp-app \
    bound_service_account_namespaces=cpp-app \
    policies=cpp-app-read ttl=1h
```

Create the app ServiceAccount, build and load the current image, then apply the manifest:

```bash
kubectl create serviceaccount cpp-app -n cpp-app --dry-run=client -o yaml | kubectl apply -f -
docker build -t cpp-k8s-demo:v1 .
kind load docker-image cpp-k8s-demo:v1 --name dev-cluster1
kubectl apply -f k8s/deployment.yaml
kubectl rollout status deployment/cpp-k8s-demo -n cpp-app
kubectl get pods -n cpp-app
```

The manifest annotates the Pod template for Vault Agent injection, selects the `cpp-app` role, and sets `API_KEY_FILE` to `/vault/secrets/api-key`. The Agent authenticates using the Pod's ServiceAccount, reads the permitted Vault path, and renders the value into a memory-backed shared volume. The C++ app reads the file at startup and logs only that the key was loaded, never its value. The Vault token remains with the Agent rather than being passed to the app.

To verify the app started without printing the key:

```bash
kubectl logs deployment/cpp-k8s-demo -n cpp-app
```

The `API_KEY_FILE` setting is optional in local runs; without it, the application continues its heartbeat behavior without expecting a key file.

#### Clean Up the Local Lab

The following commands remove the Helm releases and the entire Kind cluster, including its local resources:

```bash
helm uninstall cpp-k8s-demo-helm -n cpp-app
helm uninstall vault -n vault
kind delete cluster --name dev-cluster1
```

### Step 7: Automated CI/CD with GitHub Actions

`.github/workflows/ci.yml` runs only for pull requests targeting `main`. In GitHub terminology, this is the merge-request review and validation event. It does not run for ordinary pushes or tag pushes. The workflow performs the following sequence:

1. Checks out the repository.
2. Installs CMake, Ninja, g++, Cppcheck, debhelper, and Debian build prerequisites.
3. Configures and compiles the C++ application with CMake and Ninja.
4. Runs Cppcheck with the system-header suppression used by the local task.
5. Copies `packaging/debian/` to the root `debian/` path expected by Debian tooling.
6. Builds an unsigned binary Debian package.
7. Copies the package into the in-workspace `artifacts/` directory.
8. Uploads the Debian package with `actions/upload-artifact@v4`.
9. Builds the Docker image and runs a bounded container smoke test.

The smoke test starts the container detached, follows its logs for ten seconds, and force-removes the named container. This is important because the application is intentionally a long-running heartbeat service.

#### Tools Used

- **GitHub Actions:** Runs the workflow on a hosted Ubuntu runner for every pull request targeting `main`.
- **actions/checkout:** Checks out the pull-request source code.
- **apt-get:** Installs the compiler, build, analysis, and Debian packaging dependencies on the runner.
- **GitHub Actions artifacts:** Stores the generated `.deb` for inspection during the workflow run.
- **Docker CLI:** Builds the image, starts the smoke-test container, follows its logs, and removes it.
- **Bash and `timeout`:** Orchestrate shell steps and bound the long-running smoke test.

The separate `.github/workflows/release.yml` workflow publishes GitHub Releases and GHCR packages when a version tag such as `v1.0.2` is pushed. Keeping publishing separate means pull-request validation remains PR-only while releases still have a deliberate versioned trigger.

#### CI Debugging and Hardening

The lifecycle required several practical fixes:

- Cppcheck system-header noise was resolved with `--suppress=missingIncludeSystem`.
- The Debian metadata was kept under `packaging/debian/` and copied into the root `debian/` directory during CI.
- `.gitignore` excludes local build output and generated Debian files while allowing the source packaging metadata to remain tracked.
- The artifact upload was changed from `../*.deb` to `artifacts/*.deb`, because artifact actions reject relative paths that leave the workspace.
- The Docker timeout was moved from an argument to the application entrypoint into an explicit detached-container lifecycle, ensuring the runner can clean up the process.

#### GitHub Actions Trigger Policy

The workflow uses only:

```yaml
on:
  pull_request:
    branches: ["main"]
```

This means every proposed merge into `main` is validated before review or merge, while direct pushes and version tags do not start this CI workflow.

## Releases and GitHub Packages

The repository uses two workflows with different responsibilities:

- **`ci.yml`:** Runs only on pull requests targeting `main`. It validates the proposed change.
- **`release.yml`:** Runs only when a tag matching `v*` is pushed. It publishes the Debian package and Docker image.

To publish a release, update `packaging/debian/changelog` to the matching Debian version, merge the change into `main`, then create and push a tag:

```bash
git tag v1.0.2
git push origin v1.0.2
```

The release workflow will:

1. Build the `.deb` package.
2. Create a GitHub Release for the tag and attach the `.deb` file.
3. Publish the container image to GitHub Container Registry:

```text
ghcr.io/hossannazmultub/cpp-k8s-demo:v1.0.2
ghcr.io/hossannazmultub/cpp-k8s-demo:latest
```

The workflow uses the automatically provided `GITHUB_TOKEN`; no personal access token is stored in the repository. The repository or organization package settings may require the package visibility to be changed from private to public before users can pull the image anonymously.

## Repository Workflow

A useful local validation sequence mirrors CI:

```bash
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
cppcheck --enable=all --suppress=missingIncludeSystem --error-exitcode=1 src/
clang-tidy src/main.cpp -p build/
```

To inspect the working tree before committing:

```bash
git status --short
```

Generated files should remain untracked. The main ignored paths include:

- `build/`
- `obj-*/`
- root-level `debian/`
- `*.deb`
- `*.tar`

## Key Takeaways

1. **Multi-stage Docker builds** keep final runtime container images small while keeping build tools such as g++ and CMake out of production environments.
2. **`kind load docker-image`** bypasses external registries during local rapid-prototyping loops.
3. **Strict pathing and artifact ignore rules** in `.dockerignore` and `.gitignore` prevent build-cache collisions across host, container, and CI-runner boundaries.
4. **Long-running services need explicit test lifecycles** in CI: start them, observe them for a bounded period, and clean them up.
5. **Packaging metadata should be versioned separately from generated output**, so a reproducible CI build can create fresh Debian artifacts on every run.

## License

This project is a demonstration repository. Add the project license here if the repository is distributed beyond its current learning and CI/CD use case.
