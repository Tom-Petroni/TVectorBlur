use std::path::PathBuf;
use std::process::Command;

use anyhow::{Error, Result};
use duct::cmd;

use crate::{
    TargetPlatform,
    nuke::sources::{get_sources, nuke_source_directory},
    util::{crate_root, path_to_string},
};

fn is_nuke_source_ready(version: &str) -> bool {
    let root = nuke_source_directory(version);
    root.join("include").is_dir()
        && (root.join("cmake").join("NukeConfig.cmake").is_file()
            || root.join("NukeConfig.cmake").is_file())
}

pub async fn compile_nuke(
    versions: &[String],
    target: TargetPlatform,
    limit_threads: bool,
    _use_zig: bool,
) -> Result<Vec<PathBuf>> {
    let mut missing_versions: Vec<String> = Vec::new();
    for version in versions {
        if !is_nuke_source_ready(version) {
            missing_versions.push(version.clone());
        }
    }

    if !missing_versions.is_empty() {
        get_sources(vec![target], &missing_versions, limit_threads).await?;
    }

    let mut binaries = vec![];
    for version in versions {
        if !is_nuke_source_ready(version) {
            log::warn!(
                "Skipping {version} as no usable extracted Nuke installation could be found for {target}."
            );
            continue;
        }

        if is_crosscompiling(target) {
            return Err(Error::msg("TVectorBlur hosted builds must run natively on the target OS runner."));
        }

        binaries.push(compile_native(version, target).await?);
    }

    Ok(binaries)
}

fn is_crosscompiling(target: TargetPlatform) -> bool {
    match target {
        TargetPlatform::Linux => std::env::consts::OS != "linux",
        TargetPlatform::Windows => std::env::consts::OS != "windows",
        TargetPlatform::MacosAarch64 => {
            std::env::consts::OS != "macos" || std::env::consts::ARCH != "aarch64"
        }
        TargetPlatform::MacosX86_64 => {
            std::env::consts::OS != "macos" || std::env::consts::ARCH != "x86_64"
        }
    }
}

fn packaged_binary_name(target: TargetPlatform) -> String {
    match target {
        TargetPlatform::Windows => "TVectorBlur.dll",
        TargetPlatform::Linux => "TVectorBlur.so",
        TargetPlatform::MacosAarch64 | TargetPlatform::MacosX86_64 => "TVectorBlur.dylib",
    }
    .to_string()
}

fn powershell_command() -> &'static str {
    if Command::new("pwsh")
        .arg("-NoProfile")
        .arg("-Command")
        .arg("$PSVersionTable.PSVersion.ToString()")
        .output()
        .is_ok()
    {
        "pwsh"
    } else {
        "powershell"
    }
}

async fn compile_native(version: &str, target: TargetPlatform) -> Result<PathBuf, anyhow::Error> {
    let work_root = crate_root();
    let sources_directory = nuke_source_directory(version);
    let binary_path = work_root
        .join("TVectorBlur")
        .join("bin")
        .join(version)
        .join(match target {
            TargetPlatform::Windows => "windows",
            TargetPlatform::Linux => "linux",
            TargetPlatform::MacosAarch64 | TargetPlatform::MacosX86_64 => "macos",
        })
        .join(packaged_binary_name(target));

    match target {
        TargetPlatform::Windows => {
            let script = path_to_string(&work_root.join("scripts").join("build_windows.ps1"))?;
            let mut expression = cmd!(
                powershell_command(),
                "-NoProfile",
                "-File",
                script,
                "-NukeRoot",
                path_to_string(&sources_directory)?,
                "-Configuration",
                "Release"
            );

            if let Ok(cuda_path) = std::env::var("CUDA_PATH") {
                expression = expression.env("CUDA_PATH", cuda_path);
            }
            expression = expression.env("TVECTORBLUR_CUDA_ARCHITECTURES", "75;86;89;90");
            expression.run()?;
        }
        TargetPlatform::Linux => {
            let script = path_to_string(&work_root.join("scripts").join("build_linux.sh"))?;
            let mut expression = cmd!("bash", script, path_to_string(&sources_directory)?, "Release");
            expression = expression.env("TVECTORBLUR_CUDA_ARCHITECTURES", "75;86;89;90");
            expression.run()?;
        }
        TargetPlatform::MacosAarch64 | TargetPlatform::MacosX86_64 => {
            return Err(Error::msg("TVectorBlur does not currently support macOS builds."));
        }
    }

    if !binary_path.exists() {
        return Err(Error::msg(format!(
            "Expected compiled TVectorBlur binary was not found at '{}'.",
            binary_path.display()
        )));
    }

    Ok(binary_path)
}
