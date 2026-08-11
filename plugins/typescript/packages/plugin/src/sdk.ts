import { decode, encode } from "@bebop/runtime/codec";
import type { CodeGeneratorRequest, CodeGeneratorResponse, Diagnostic, GeneratedFile } from "./plugin.bb.js";
import { CodeGeneratorRequest as RequestCodec, CodeGeneratorResponse as ResponseCodec } from "./plugin.bb.js";

export type BebopPlugin = (
  request: CodeGeneratorRequest,
) => CodeGeneratorResponse | Promise<CodeGeneratorResponse>;

export type BebopContributor = {
  readonly name: string;
  /** Contributors that must be installed and run first. */
  readonly requires?: readonly string[];
  /** Optional ordering constraints. Missing contributors are ignored. */
  readonly after?: readonly string[];
  /** Optional ordering constraints. Missing contributors are ignored. */
  readonly before?: readonly string[];
  contribute(context: ContributorContext): void | PromiseLike<void>;
};

export type ContributorContext = {
  readonly request: CodeGeneratorRequest;
  readonly response: ResponseBuilder;
  readonly state: ContributorState;
};

/** A zero-collision, type-carrying key for state shared between contributors. */
export class ContributorKey<Value> {
  private declare readonly value: Value;

  constructor(readonly description: string) {}
}

export function createContributorKey<Value>(description: string): ContributorKey<Value> {
  return new ContributorKey(description);
}

export class ContributorState {
  private readonly values = new Map<ContributorKey<unknown>, unknown>();

  has(key: ContributorKey<unknown>): boolean {
    return this.values.has(key);
  }

  get<Value>(key: ContributorKey<Value>): Value | undefined {
    return this.values.get(key) as Value | undefined;
  }

  set<Value>(key: ContributorKey<Value>, value: Value): this {
    this.values.set(key, value);
    return this;
  }
}

/** Preserve inference while checking that a function implements the Bebop plugin contract. */
export function definePlugin<T extends BebopPlugin>(plugin: T): T {
  return plugin;
}

/** Preserve literal contributor names and ordering constraints. */
export function defineContributor<const Contributor extends BebopContributor>(
  contributor: Contributor,
): Contributor {
  return contributor;
}

/** Compose contributors into one deterministic compiler plugin. */
export function composePlugin(
  ...contributors: readonly BebopContributor[]
): BebopPlugin {
  const ordered = orderContributors(contributors);
  return definePlugin(async (request) => {
    const response = new ResponseBuilder();
    const context: ContributorContext = {
      request,
      response,
      state: new ContributorState(),
    };
    for (const contributor of ordered) {
      try {
        await contributor.contribute(context);
      } catch (error) {
        response.fail(`${contributor.name}: ${errorMessage(error)}`);
        break;
      }
    }
    return response.build();
  });
}

/** Mutable response assembly without leaking mutation into the wire model. */
export class ResponseBuilder {
  private readonly files: GeneratedFile[] = [];
  private readonly diagnostics: Diagnostic[] = [];
  private readonly completeFileNames = new Set<string>();
  private error: string | undefined;

  addFile(file: GeneratedFile): this;
  addFile(name: string, content: string): this;
  addFile(fileOrName: GeneratedFile | string, content?: string): this {
    let file: GeneratedFile;
    if (typeof fileOrName === "string") {
      if (content === undefined) throw new TypeError("generated file content is required");
      file = { name: fileOrName, content };
    } else {
      file = fileOrName;
    }
    if (file.insertionPoint !== undefined && file.name === undefined) {
      throw new TypeError("an insertion-point contribution must name its target file");
    }
    if (file.name !== undefined && file.insertionPoint === undefined) {
      if (this.completeFileNames.has(file.name)) {
        throw new Error(`generated file '${file.name}' was added more than once`);
      }
      this.completeFileNames.add(file.name);
    }
    this.files.push(file);
    return this;
  }

  insert(name: string, insertionPoint: string, content: string): this {
    return this.addFile({ name, insertionPoint, content });
  }

  addDiagnostic(diagnostic: Diagnostic): this {
    this.diagnostics.push(diagnostic);
    return this;
  }

  fail(message: string): this {
    this.error ??= message;
    return this;
  }

  merge(response: CodeGeneratorResponse): this {
    if (response.error !== undefined) this.fail(response.error);
    for (const file of response.files ?? []) this.addFile(file);
    for (const diagnostic of response.diagnostics ?? []) this.addDiagnostic(diagnostic);
    return this;
  }

  build(): CodeGeneratorResponse {
    return {
      ...(this.error === undefined ? {} : { error: this.error }),
      ...(this.files.length === 0 ? {} : { files: [...this.files] }),
      ...(this.diagnostics.length === 0 ? {} : { diagnostics: [...this.diagnostics] }),
    };
  }

  encode(): Uint8Array {
    return encode(ResponseCodec, this.build());
  }
}

/** Decode a compiler request, invoke a plugin, and always return a protocol response. */
export async function executePlugin(plugin: BebopPlugin, input: Uint8Array): Promise<Uint8Array> {
  if (input.length === 0) {
    return encode(ResponseCodec, { error: "Bebop plugin received empty input" });
  }
  try {
    const request = decode(RequestCodec, input);
    return encode(ResponseCodec, await plugin(request));
  } catch (error) {
    return encode(ResponseCodec, { error: errorMessage(error) });
  }
}

export function errorMessage(error: unknown): string {
  return error instanceof Error ? error.message : String(error);
}

function orderContributors(contributors: readonly BebopContributor[]): readonly BebopContributor[] {
  const byName = new Map<string, BebopContributor>();
  for (const contributor of contributors) {
    if (contributor.name.trim().length === 0) throw new TypeError("contributor names must not be empty");
    if (byName.has(contributor.name)) throw new Error(`duplicate contributor '${contributor.name}'`);
    byName.set(contributor.name, contributor);
  }

  const dependencies = new Map<string, Set<string>>();
  for (const contributor of contributors) {
    dependencies.set(contributor.name, new Set());
  }
  const addEdge = (before: string, after: string): void => {
    if (before === after) throw new Error(`contributor '${before}' cannot depend on itself`);
    const edges = dependencies.get(after);
    if (edges === undefined) throw new Error(`unknown contributor '${after}'`);
    edges.add(before);
  };

  for (const contributor of contributors) {
    for (const dependency of contributor.requires ?? []) {
      if (!byName.has(dependency)) {
        throw new Error(`contributor '${contributor.name}' requires missing contributor '${dependency}'`);
      }
      addEdge(dependency, contributor.name);
    }
    for (const dependency of contributor.after ?? []) {
      if (byName.has(dependency)) addEdge(dependency, contributor.name);
    }
    for (const dependent of contributor.before ?? []) {
      if (byName.has(dependent)) addEdge(contributor.name, dependent);
    }
  }

  const result: BebopContributor[] = [];
  const state = new Map<string, "visiting" | "done">();
  const stack: string[] = [];
  const stackIndexes = new Map<string, number>();
  type Visit = {
    readonly name: string;
    readonly dependencies: readonly string[];
    index: number;
  };
  const visits: Visit[] = [];
  for (const root of contributors) {
    if (state.get(root.name) === "done") continue;
    const rootDependencies = dependencies.get(root.name);
    if (rootDependencies === undefined) throw new Error(`unknown contributor '${root.name}'`);
    visits.push({ name: root.name, dependencies: [...rootDependencies], index: 0 });
    while (visits.length !== 0) {
      const visit = visits[visits.length - 1]!;
      if (state.get(visit.name) === undefined) {
        state.set(visit.name, "visiting");
        stackIndexes.set(visit.name, stack.length);
        stack.push(visit.name);
      }
      const dependency = visit.dependencies[visit.index++];
      if (dependency !== undefined) {
        const dependencyState = state.get(dependency);
        if (dependencyState === "done") continue;
        if (dependencyState === "visiting") {
          const cycleStart = stackIndexes.get(dependency);
          if (cycleStart === undefined) throw new Error("invalid contributor traversal state");
          throw new Error(`contributor dependency cycle: ${stack.slice(cycleStart).join(", ")}`);
        }
        const nestedDependencies = dependencies.get(dependency);
        if (nestedDependencies === undefined) throw new Error(`unknown contributor '${dependency}'`);
        visits.push({ name: dependency, dependencies: [...nestedDependencies], index: 0 });
        continue;
      }
      visits.pop();
      stack.pop();
      stackIndexes.delete(visit.name);
      state.set(visit.name, "done");
      const contributor = byName.get(visit.name);
      if (contributor === undefined) throw new Error(`unknown contributor '${visit.name}'`);
      result.push(contributor);
    }
  }
  return result;
}
