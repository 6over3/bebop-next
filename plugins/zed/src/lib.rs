use std::fs;

use zed_extension_api::{self as zed, settings::LspSettings, LanguageServerId, Result};

const REPO: &str = "6over3/bebop-next";
const SERVER_NAME: &str = "bebopc";

struct BebopBinary {
    path: String,
    args: Option<Vec<String>>,
    env: Option<Vec<(String, String)>>,
}

struct BebopExtension {
    cached_binary_path: Option<String>,
}

impl BebopExtension {
    fn language_server_binary(
        &mut self,
        language_server_id: &LanguageServerId,
        worktree: &zed::Worktree,
    ) -> Result<BebopBinary> {
        let binary_settings = LspSettings::for_worktree(SERVER_NAME, worktree)
            .ok()
            .and_then(|settings| settings.binary);
        let binary_args = binary_settings
            .as_ref()
            .and_then(|settings| settings.arguments.clone());

        if let Some(path) = binary_settings.and_then(|settings| settings.path) {
            return Ok(BebopBinary {
                path,
                args: binary_args,
                env: None,
            });
        }

        if let Some(path) = worktree.which(SERVER_NAME) {
            return Ok(BebopBinary {
                path,
                args: binary_args,
                // Schemas reference $(VAR); substitution needs the user's shell env.
                env: Some(worktree.shell_env()),
            });
        }

        if let Some(path) = &self.cached_binary_path {
            if fs::metadata(path).is_ok_and(|stat| stat.is_file()) {
                return Ok(BebopBinary {
                    path: path.clone(),
                    args: binary_args,
                    env: None,
                });
            }
        }

        zed::set_language_server_installation_status(
            language_server_id,
            &zed::LanguageServerInstallationStatus::CheckingForUpdate,
        );

        // No stable release exists yet; fall back to pre-releases.
        let release = zed::latest_github_release(
            REPO,
            zed::GithubReleaseOptions {
                require_assets: true,
                pre_release: false,
            },
        )
        .or_else(|_| {
            zed::latest_github_release(
                REPO,
                zed::GithubReleaseOptions {
                    require_assets: true,
                    pre_release: true,
                },
            )
        })?;

        let (platform, arch) = zed::current_platform();
        let os = match platform {
            zed::Os::Mac => "darwin",
            zed::Os::Linux => "linux",
            zed::Os::Windows => "windows",
        };
        let arch = match arch {
            zed::Architecture::Aarch64 => "arm64",
            zed::Architecture::X8664 => "x64",
            zed::Architecture::X86 => {
                return Err("bebopc has no 32-bit x86 builds".into());
            }
        };

        let asset_name = format!("bebop@{version}+{os}-{arch}.tar.gz", version = release.version);
        let asset = release
            .assets
            .iter()
            .find(|asset| asset.name == asset_name)
            .ok_or_else(|| format!("release {} has no asset {asset_name}", release.version))?;

        let version_dir = format!("bebopc-{}", release.version);
        let extension = if platform == zed::Os::Windows { ".exe" } else { "" };
        let binary_path = format!("{version_dir}/bin/bebopc{extension}");

        if !fs::metadata(&binary_path).is_ok_and(|stat| stat.is_file()) {
            zed::set_language_server_installation_status(
                language_server_id,
                &zed::LanguageServerInstallationStatus::Downloading,
            );

            zed::download_file(
                &asset.download_url,
                &version_dir,
                zed::DownloadedFileType::GzipTar,
            )
            .map_err(|err| format!("failed to download {asset_name}: {err}"))?;

            zed::make_file_executable(&binary_path)?;

            if let Ok(entries) = fs::read_dir(".") {
                for entry in entries.flatten() {
                    let name = entry.file_name();
                    let Some(name) = name.to_str() else { continue };
                    if name.starts_with("bebopc-") && name != version_dir {
                        fs::remove_dir_all(entry.path()).ok();
                    }
                }
            }
        }

        self.cached_binary_path = Some(binary_path.clone());
        Ok(BebopBinary {
            path: binary_path,
            args: binary_args,
            env: None,
        })
    }
}

impl zed::Extension for BebopExtension {
    fn new() -> Self {
        Self {
            cached_binary_path: None,
        }
    }

    fn language_server_command(
        &mut self,
        language_server_id: &LanguageServerId,
        worktree: &zed::Worktree,
    ) -> Result<zed::Command> {
        let binary = self.language_server_binary(language_server_id, worktree)?;
        Ok(zed::Command {
            command: binary.path,
            args: binary.args.unwrap_or_else(|| vec!["lsp".to_string()]),
            env: binary.env.unwrap_or_default(),
        })
    }

    fn language_server_initialization_options(
        &mut self,
        _language_server_id: &LanguageServerId,
        worktree: &zed::Worktree,
    ) -> Result<Option<zed::serde_json::Value>> {
        Ok(LspSettings::for_worktree(SERVER_NAME, worktree)
            .ok()
            .and_then(|settings| settings.initialization_options))
    }

    fn language_server_workspace_configuration(
        &mut self,
        _language_server_id: &LanguageServerId,
        worktree: &zed::Worktree,
    ) -> Result<Option<zed::serde_json::Value>> {
        Ok(LspSettings::for_worktree(SERVER_NAME, worktree)
            .ok()
            .and_then(|settings| settings.settings))
    }
}

zed::register_extension!(BebopExtension);
