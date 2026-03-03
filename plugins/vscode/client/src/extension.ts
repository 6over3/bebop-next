import { execFileSync, type ChildProcess, spawn } from "child_process";
import * as fs from "fs";
import * as path from "path";
import * as semver from "semver";
import {
	commands,
	type ExtensionContext,
	type OutputChannel,
	StatusBarAlignment,
	type StatusBarItem,
	Task,
	TaskScope,
	type TaskProvider,
	ShellExecution,
	tasks,
	Uri,
	window,
	workspace,
	env,
} from "vscode";
import {
	type Executable,
	LanguageClient,
} from "vscode-languageclient/node";

const MIN_VERSION = "2026.0.0";

type CompilerState =
	| { status: "not-found" }
	| { status: "incompatible"; path: string; version: string }
	| { status: "no-lsp"; path: string; version: semver.SemVer }
	| { status: "ready"; path: string; version: semver.SemVer };

function resolveCompilerPath(): string | null {
	const configured = workspace.getConfiguration("bebop").get<string>("compiler.path")?.trim();
	if (configured) {
		return fs.existsSync(configured) ? configured : null;
	}
	const ext = process.platform === "win32" ? ".exe" : "";
	for (const dir of (process.env.PATH ?? "").split(path.delimiter)) {
		const full = path.join(dir, `bebopc${ext}`);
		if (fs.existsSync(full)) return full;
	}
	return null;
}

function queryVersion(compilerPath: string): semver.SemVer | null {
	try {
		const output = execFileSync(compilerPath, ["--version"], {
			encoding: "utf-8",
			timeout: 5000,
		}).trim();
		return semver.parse(output);
	} catch {
		return null;
	}
}

function supportsLsp(compilerPath: string): boolean {
	try {
		execFileSync(compilerPath, ["lsp", "--help"], { timeout: 5000, stdio: "ignore" });
		return true;
	} catch {
		return false;
	}
}

function detectCompiler(): CompilerState {
	const compilerPath = resolveCompilerPath();
	if (!compilerPath) return { status: "not-found" };

	const version = queryVersion(compilerPath);
	if (!version || semver.lt(version, MIN_VERSION)) {
		return { status: "incompatible", path: compilerPath, version: version?.version ?? "unknown" };
	}

	if (!supportsLsp(compilerPath)) {
		return { status: "no-lsp", path: compilerPath, version };
	}

	return { status: "ready", path: compilerPath, version };
}

function findConfigFile(): string | null {
	const configured = workspace.getConfiguration("bebop").get<string>("compiler.configPath")?.trim();
	if (configured && fs.existsSync(configured)) return configured;

	for (const folder of workspace.workspaceFolders ?? []) {
		for (const name of ["bebop.yml", "bebop.yaml"]) {
			const full = path.join(folder.uri.fsPath, name);
			if (fs.existsSync(full)) return full;
		}
	}
	return null;
}

class BebopExtension {
	private readonly output: OutputChannel;
	private readonly statusBar: StatusBarItem;
	private compiler: CompilerState = { status: "not-found" };
	private configPath: string | null = null;
	private client: LanguageClient | null = null;
	private watchProcess: ChildProcess | null = null;

	constructor(private readonly context: ExtensionContext) {
		this.output = window.createOutputChannel("Bebop");
		this.statusBar = window.createStatusBarItem(StatusBarAlignment.Left, 100);
		context.subscriptions.push(this.output, this.statusBar);
	}

	async activate(): Promise<void> {
		this.compiler = detectCompiler();
		this.configPath = findConfigFile();

		this.log(`compiler: ${this.compiler.status}${"path" in this.compiler ? ` at ${this.compiler.path}` : ""}`);
		if (this.configPath) this.log(`config: ${this.configPath}`);

		commands.executeCommand("setContext", "bebop.hasConfig", this.configPath !== null);
		this.updateStatusBar();
		this.registerCommands();
		this.registerWatchers();

		if (this.compiler.status === "ready") {
			await this.startClient();
		} else {
			this.notifyCompilerIssue();
		}
	}

	async deactivate(): Promise<void> {
		this.watchProcess?.kill();
		this.watchProcess = null;
		await this.client?.stop();
		this.client = null;
	}

	private log(msg: string): void {
		this.output.appendLine(`[${new Date().toISOString()}] ${msg}`);
	}

	private registerCommands(): void {
		this.context.subscriptions.push(
			commands.registerCommand("bebop.build", () => this.build()),
			commands.registerCommand("bebop.watch", () => this.watch()),
			commands.registerCommand("bebop.stopWatch", () => this.stopWatch()),
			commands.registerCommand("bebop.init", () => this.initProject()),
			commands.registerCommand("bebop.restartServer", () => this.restartServer()),
			commands.registerCommand("bebop.showOutput", () => this.output.show()),
			commands.registerCommand("bebop.openDocs", () => env.openExternal(Uri.parse("https://bebop.sh"))),
			tasks.registerTaskProvider("bebop", this.taskProvider()),
		);
	}

	private registerWatchers(): void {
		const refreshConfig = () => {
			this.configPath = findConfigFile();
			commands.executeCommand("setContext", "bebop.hasConfig", this.configPath !== null);
		};

		const configWatcher = workspace.createFileSystemWatcher("**/bebop.{yml,yaml}");
		configWatcher.onDidCreate(refreshConfig);
		configWatcher.onDidDelete(refreshConfig);

		this.context.subscriptions.push(
			configWatcher,
			workspace.onDidChangeConfiguration((e) => {
				if (!e.affectsConfiguration("bebop")) return;
				this.compiler = detectCompiler();
				this.updateStatusBar();
				this.restartServer();
			}),
		);
	}

	private updateStatusBar(): void {
		const { statusBar: bar, compiler } = this;

		switch (compiler.status) {
			case "not-found":
				bar.text = "$(warning) Bebop: Not Found";
				bar.tooltip = "bebopc not found. Click to configure.";
				bar.command = "workbench.action.openSettings";
				break;

			case "incompatible":
				bar.text = `$(warning) Bebop: ${compiler.version}`;
				bar.tooltip = `bebopc ${compiler.version} requires >= ${MIN_VERSION}`;
				bar.command = "bebop.openDocs";
				break;

			case "no-lsp":
				bar.text = "$(warning) Bebop: No LSP";
				bar.tooltip = `Language server requires >= ${MIN_VERSION}`;
				bar.command = "bebop.openDocs";
				break;

			case "ready": {
				const v = compiler.version.version;
				bar.text = this.watchProcess ? `$(eye) Bebop: ${v}` : `$(check) Bebop: ${v}`;
				bar.tooltip = this.watchProcess ? "Watch mode active. Click to stop." : `Bebop ${v}`;
				bar.command = this.watchProcess ? "bebop.stopWatch" : "bebop.build";
				break;
			}
		}

		bar.show();
	}

	private notifyCompilerIssue(): void {
		let message: string;
		switch (this.compiler.status) {
			case "not-found":
				message = "bebopc not found. Install bebopc or configure the path in settings.";
				break;
			case "incompatible":
				message = `bebopc ${this.compiler.version} is not compatible. Requires >= ${MIN_VERSION}.`;
				break;
			case "no-lsp":
				message = `bebopc does not support the language server. Requires >= ${MIN_VERSION}.`;
				break;
			default:
				return;
		}

		window.showErrorMessage(message, "Open Settings", "Learn More").then((action) => {
			if (action === "Open Settings") {
				commands.executeCommand("workbench.action.openSettings", "bebop.compiler.path");
			} else if (action === "Learn More") {
				env.openExternal(Uri.parse("https://bebop.sh"));
			}
		});
	}

	private async startClient(): Promise<void> {
		if (this.compiler.status !== "ready") return;

		const config = workspace.getConfiguration("bebop");
		const args = ["lsp"];
		for (const p of config.get<string[]>("compiler.includePaths") ?? []) {
			args.push("-I", p);
		}
		const cfgPath = config.get<string>("compiler.configPath")?.trim();
		if (cfgPath) args.push("--config", cfgPath);

		const run: Executable = { command: this.compiler.path, args };

		this.client = new LanguageClient("bebop", "Bebop Language Server", { run, debug: run }, {
			documentSelector: [
				{ scheme: "file", language: "bebop" },
				{ scheme: "untitled", language: "bebop" },
			],
			synchronize: {
				fileEvents: workspace.createFileSystemWatcher("**/*.bop"),
			},
			outputChannel: this.output,
		});

		try {
			await this.client.start();
			this.log("language server started");
		} catch (err) {
			this.log(`language server failed: ${err}`);
			window.showErrorMessage("Bebop: Language server failed to start. See Output for details.");
		}
	}

	private async restartServer(): Promise<void> {
		await this.client?.stop();
		this.client = null;
		await this.startClient();
	}

	private requireCompilerAndConfig(): { compiler: string; config: string } | null {
		if (this.compiler.status !== "ready") {
			window.showErrorMessage("bebopc not found or incompatible.");
			return null;
		}
		if (!this.configPath) {
			window.showErrorMessage("No bebop.yml found. Run 'Bebop: Initialize Project' first.");
			return null;
		}
		return { compiler: this.compiler.path, config: this.configPath };
	}

	private build(): void {
		const ctx = this.requireCompilerAndConfig();
		if (!ctx) return;
		const terminal = window.createTerminal("Bebop Build");
		terminal.show();
		terminal.sendText(`"${ctx.compiler}" build --config "${ctx.config}"`);
	}

	private watch(): void {
		if (this.watchProcess) return;
		const ctx = this.requireCompilerAndConfig();
		if (!ctx) return;

		this.watchProcess = spawn(ctx.compiler, ["watch", "--config", ctx.config], {
			stdio: ["ignore", "pipe", "pipe"],
		});

		this.watchProcess.stdout?.on("data", (data: Buffer) => this.log(`[watch] ${data.toString().trim()}`));
		this.watchProcess.stderr?.on("data", (data: Buffer) => this.log(`[watch] ${data.toString().trim()}`));
		this.watchProcess.on("close", (code) => {
			this.log(`watch exited (${code})`);
			this.watchProcess = null;
			this.updateStatusBar();
		});

		this.updateStatusBar();
	}

	private stopWatch(): void {
		if (!this.watchProcess) return;
		this.watchProcess.kill();
		this.watchProcess = null;
		this.updateStatusBar();
	}

	private async initProject(): Promise<void> {
		const folders = workspace.workspaceFolders;
		if (!folders?.length) {
			window.showErrorMessage("No workspace folder open.");
			return;
		}

		let folder = folders[0];
		if (folders.length > 1) {
			const picked = await window.showWorkspaceFolderPick({ placeHolder: "Select folder for bebop.yml" });
			if (!picked) return;
			folder = picked;
		}

		const target = path.join(folder.uri.fsPath, "bebop.yml");
		if (fs.existsSync(target)) {
			window.showWarningMessage("bebop.yml already exists in this folder.");
			return;
		}

		fs.writeFileSync(target, 'edition: "2026"\n\nincludes:\n  - "**/*.bop"\n');

		const doc = await workspace.openTextDocument(target);
		await window.showTextDocument(doc);
		commands.executeCommand("setContext", "bebop.hasConfig", true);
	}

	private taskProvider(): TaskProvider {
		return {
			provideTasks: () => {
				if (this.compiler.status !== "ready" || !this.configPath) return [];
				const { path: bin } = this.compiler;
				const cfg = this.configPath;
				return [
					new Task({ type: "bebop", command: "build" }, TaskScope.Workspace, "Build", "bebop",
						new ShellExecution(`"${bin}" build --config "${cfg}"`), "$bebopc"),
					new Task({ type: "bebop", command: "watch" }, TaskScope.Workspace, "Watch", "bebop",
						new ShellExecution(`"${bin}" watch --config "${cfg}"`), "$bebopc"),
				];
			},
			resolveTask: (task) => {
				if (this.compiler.status !== "ready") return undefined;
				const cmd = task.definition.command as string;
				const cfg = (task.definition.config as string | undefined) ?? this.configPath;
				if (!cfg) return undefined;
				return new Task(task.definition, TaskScope.Workspace, task.name, "bebop",
					new ShellExecution(`"${this.compiler.path}" ${cmd} --config "${cfg}"`), "$bebopc");
			},
		};
	}
}

let extension: BebopExtension | undefined;

export async function activate(context: ExtensionContext): Promise<void> {
	extension = new BebopExtension(context);
	await extension.activate();
}

export async function deactivate(): Promise<void> {
	await extension?.deactivate();
}
