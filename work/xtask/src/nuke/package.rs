use crate::{
    TargetPlatform,
    util::{crate_root, path_to_string},
};
use anyhow::Result;
use duct::cmd;

pub async fn create_package(target: TargetPlatform, versions: Vec<String>) -> Result<()> {
    let work_root = crate_root();
    match target {
        TargetPlatform::Windows | TargetPlatform::Linux => {
            let script = path_to_string(&work_root.join("scripts").join("sync_publish_bins.py"))?;
            let python = if cfg!(windows) { "python" } else { "python3" };
            cmd!(python, script).run()?;
        }
        TargetPlatform::MacosAarch64 | TargetPlatform::MacosX86_64 => {
            log::warn!("Skipping macOS package sync for TVectorBlur.");
        }
    }

    log::info!("Synced publish payload for versions: {:?}", versions);
    Ok(())
}
