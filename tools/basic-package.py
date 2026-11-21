"""Assemble the Debian package, local APT repository and independently verifiable release inventory."""

import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
from tempfile import TemporaryDirectory


def output(*args, **opts):
    return subprocess.check_output(args, text=True, **opts).strip()


def digest(path):
    with path.open("rb") as file:
        return hashlib.file_digest(file, "sha256").hexdigest()


def copy(src, dst):
    dst.parent.mkdir(parents=True, exist_ok=True)
    shutil.copy2(src, dst)


def write_json(path, obj):
    path.write_text(json.dumps(obj, indent=2, sort_keys=True) + "\n")


def make_package(work, repo, version, runtime, headers):
    pkg = work / "package"
    share = pkg / "usr/share/bacuri-vms"
    for binary in ("core", "media", "recorder", "observation-source", "verify-export"):
        file = "bacuri-vms-" + binary
        copy(Path("build/gcc-release") / file, pkg / "usr/lib/bacuri-vms" / file)
    copy("build/gcc-release/bacuri-basic", pkg / "usr/sbin/bacuri-basic")
    copy("build/gcc-release/bacuri-maintain", pkg / "usr/sbin/bacuri-maintain")
    for unit in ("bacuri-diagnostics.timer","bacuri-diagnostics.service","bacuri-metrics.socket","bacuri-metrics@.service"):
        copy(Path("packaging/basic")/unit,pkg/"usr/lib/systemd/system"/unit)
    for name in ("core", "recorder", "media"):
        unit = f"bacuri-{name}.service"
        copy(Path("packaging/basic") / unit, pkg / "usr/lib/systemd/system" / unit)
    copy("packaging/basic/bacuri-vms.conf", pkg / "usr/lib/sysusers.d/bacuri-vms.conf")
    copy("packaging/basic/runtime.conf", pkg / "usr/lib/tmpfiles.d/bacuri-vms.conf")
    copy("packaging/basic/bootstrap.sql", share / "bootstrap.sql")
    shutil.copytree("storage/postgresql/migrations", share / "storage/postgresql/migrations")
    shutil.copytree("web", share / "web", ignore=shutil.ignore_patterns("client"))
    shutil.copytree("build/web-client", share / "build/web-client", ignore=shutil.ignore_patterns("*.map"))
    copy("LICENSE", pkg / "usr/share/doc/bacuri-vms-basic/copyright")
    for node in headers:
        shutil.copytree(Path(node["package_folder"]) / "licenses", pkg / "usr/share/doc/bacuri-vms-basic" / node["name"])
    for license_file in Path("design/bacuri/assets").rglob("*"):
        if license_file.is_file() and (
            "license" in license_file.name.lower() or "ofl" in license_file.name.lower()
        ):
            copy(license_file, pkg / "usr/share/doc/bacuri-vms-basic/fonts" / license_file.name)
    depends = [f"{p['name']} (= {p['version']})" for p in runtime]
    control = pkg / "DEBIAN"
    control.mkdir()
    (control / "control").write_text(
        f"Package: bacuri-vms-basic\nVersion: {version}\nArchitecture: amd64\n"
        "Maintainer: Bacuri VMS <packages@localhost>\nDescription: Bacuri VMS Basic local video management services\n"
        + "Depends: "
        + ",\n ".join(depends)
        + "\n"
    )
    copy("packaging/basic/postinst", control / "postinst")
    (control / "postinst").chmod(0o755)
    product = repo / f"bacuri-vms-basic_{version}_amd64.deb"
    subprocess.run(["dpkg-deb", "--root-owner-group", "-Zxz", "--build", pkg, product], check=True)
    return product


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("directory", type=Path)
    group = parser.add_mutually_exclusive_group(required=True)
    group.add_argument("--qualification", action="store_true")
    group.add_argument("--signer")
    args = parser.parse_args()
    root = args.directory.resolve()
    root.mkdir(parents=True, exist_ok=True)
    kit = root / "bundle"
    kit.mkdir()
    repo = kit / "repository"
    repo.mkdir()
    files = Path("/opt/bacuri/offline-packages")
    for file in files.glob("*.deb"):
        copy(file, repo / file.name)
    cache = Path("build/gcc-release/CMakeCache.txt").read_text().splitlines()
    version = next(x.split("=", 1)[1] for x in cache if x.startswith("CMAKE_PROJECT_VERSION:")) + "-1"
    if args.qualification:
        version += "~qualification"
    with TemporaryDirectory() as tmp:
        work = Path(tmp)
        env = dict(os.environ)
        if args.qualification:
            env["GNUPGHOME"] = str(work / "gnupg")
            Path(env["GNUPGHOME"]).mkdir(mode=0o700)
            signer = "Bacuri offline installation qualification"
            subprocess.run(
                [
                    "gpg",
                    "--batch",
                    "--passphrase",
                    "",
                    "--quick-generate-key",
                    signer,
                    "ed25519",
                    "sign",
                    "0",
                ],
                check=True,
                env=env,
            )
        else:
            signer = args.signer
            if len(signer) not in (40, 64) or any(c not in "0123456789abcdefABCDEF" for c in signer):
                raise ValueError("Specify the release key fingerprint")

        def sign(path, dest, mode="--detach-sign"):
            subprocess.run(
                ["gpg", "--batch", "--local-user", signer, "--output", dest, mode, path], env=env, check=True
            )

        def package_info(file):
            fields = "${Package}\\t${Version}\\t${Architecture}"
            name, ver, arch = output("dpkg-deb", "-W", "--showformat=" + fields, file).split("\t")
            return dict(name=name, version=ver, architecture=arch, file=file.name, sha256=digest(file))

        packages = [package_info(file) for file in sorted(repo.glob("*.deb"))]
        graph = json.loads(Path("/opt/bacuri/deps/gcc-release/graph.json").read_text())["graph"]["nodes"]
        headers = [n for n in graph.values() if n.get("name") in ("boost", "nlohmann_json")]
        product = make_package(work, repo, version, packages, headers)
        packages.append(package_info(product))
        copy("build/gcc-release/bacuri-install", kit / "bacuri-install")
        copy("build/gcc-release/bacuri-maintain", kit / "bacuri-maintain")
        copy("docs/basic-maintenance.md", kit / "MAINTENANCE.md")
        copy("LICENSE", kit / "LICENSE")
        copy("docs/basic-install.md", kit / "INSTALL.md")
        (kit / "VERSION").write_text(version + "\n")
        static = Path("build/web-client")
        assets = {
            str(p.relative_to(static)): digest(p)
            for p in static.rglob("*")
            if p.is_file() and p.suffix != ".map"
        }
        lock = json.loads(Path("web/client/package-lock.json").read_text())["packages"]
        npm = [
            dict(path=k, version=v["version"], **{i: v[i] for i in ("integrity", "license") if i in v})
            for k, v in lock.items()
            if k
        ]
        sources = {}
        for path in sorted(static.rglob("*.js.map")):
            names = set()
            for src in json.loads(path.read_text())["sources"]:
                if "/node_modules/" in src:
                    chunks = src.split("/node_modules/")[-1].split("/")
                    names.add("/".join(chunks[:2]) if chunks[0].startswith("@") else chunks[0])
            sources[path.name.removesuffix(".map")] = sorted(names)
        write_json(
            kit / "sbom.json",
            {
                "package_version": version,
                "debian_packages": packages,
                "web_assets": assets,
                "legacy_web_assets": {
                    str(p.relative_to("web")): digest(p)
                    for p in Path("web").rglob("*")
                    if p.is_file() and "client" not in p.relative_to("web").parts
                },
                "browser_chunks": sources,
                "frontend_runtime_graph": [p for p in npm if not lock[p["path"]].get("dev")],
                "cpp_headers": [n["ref"] for n in headers],
            },
        )
        write_json(
            root / "build-tools.json",
            {
                "api_build_graph": {k: v["version"] for k, v in json.loads(Path("tools/api-client/package-lock.json").read_text())["packages"].items() if k},
                "packager_debian": output("dpkg-query", "-W").splitlines(),
                "frontend_build_graph": [p for p in npm if lock[p["path"]].get("dev")],
                "conan": output("conan", "--version"),
                "node": Path("build/web-build-node").read_text().strip(),
                "npm": Path("build/web-build-npm").read_text().strip(),
                "compiler": output("g++-14", "-dumpfullversion"),
                "cmake": output("cmake", "--version").splitlines()[0],
            },
        )
        with (repo / "Packages").open("wb") as out:
            subprocess.run(["apt-ftparchive", "packages", "."], cwd=repo, stdout=out, check=True)
        with (repo / "Release").open("wb") as out:
            subprocess.run(
                ["apt-ftparchive", "-o", "APT::FTPArchive::Release::Origin=Bacuri", "release", "."],
                cwd=repo,
                stdout=out,
                check=True,
            )
        sign(repo / "Release", repo / "InRelease", "--clearsign")
        sign(product, repo / (product.name + ".sig"))
        sign(kit / "bacuri-install", kit / "bacuri-install.sig")
        sign(kit / "bacuri-maintain", kit / "bacuri-maintain.sig")
        entries = [f"{digest(p)}  {p.relative_to(kit)}\n" for p in sorted(kit.rglob("*")) if p.is_file()]
        (kit / "SHA256SUMS").write_text("".join(entries))
        sign(kit / "SHA256SUMS", kit / "SHA256SUMS.sig")
        with (root / "release-key.gpg").open("wb") as key:
            subprocess.run(["gpg", "--batch", "--export", signer], stdout=key, check=True, env=env)
        print(f"Signed {len(packages)} Debian packages and {len(assets)} web assets in {kit}")


if __name__ == "__main__":
    main()
