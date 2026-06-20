export class IndentedStringBuilder {
  private readonly chunks: string[] = [];

  constructor(private spaces = 0) {}

  append(text: string): this {
    const indent = " ".repeat(this.spaces);
    const lines = getLines(text);
    this.chunks.push(lines.map((line) => `${indent}${line}`.trimEnd()).join("\n").trimEnd());
    return this;
  }

  appendMid(text: string): this {
    if (getLines(text).length > 1) {
      throw new Error("appendMid must not contain multiple lines");
    }
    this.chunks.push(text);
    return this;
  }

  appendEnd(text: string): this {
    if (getLines(text).length > 1) {
      throw new Error("appendEnd must not contain multiple lines");
    }
    this.chunks.push(`${text.trimEnd()}\n`);
    return this;
  }

  line(text = ""): this {
    if (text.length === 0) {
      this.chunks.push("\n");
      return this;
    }
    this.append(text);
    this.chunks.push("\n");
    return this;
  }

  indent(spaces = 2): this {
    this.spaces = Math.max(0, this.spaces + spaces);
    return this;
  }

  dedent(spaces = 2): this {
    this.spaces = Math.max(0, this.spaces - spaces);
    return this;
  }

  indented(fn: (builder: this) => void, spaces = 2): this {
    this.indent(spaces);
    try {
      fn(this);
    } finally {
      this.dedent(spaces);
    }
    return this;
  }

  block(openingLine: string, fn: (builder: this) => void, close = "}", open = "{"): this {
    if (openingLine.length > 0) {
      this.append(openingLine).appendEnd(` ${open}`);
    } else {
      this.line(open);
    }

    this.indented(fn);
    this.line(close);
    return this;
  }

  trimStart(): this {
    const content = this.toString().trimStart();
    this.chunks.length = 0;
    this.chunks.push(content);
    return this;
  }

  trimEnd(): this {
    const content = this.toString().trimEnd();
    this.chunks.length = 0;
    this.chunks.push(content);
    return this;
  }

  trim(): this {
    const content = this.toString().trim();
    this.chunks.length = 0;
    this.chunks.push(content);
    return this;
  }

  encode(): Uint8Array {
    return new TextEncoder().encode(this.toString());
  }

  toString(): string {
    return this.chunks.join("");
  }
}

function getLines(text: string): string[] {
  return text.split(/\r\n|\n|\r/u);
}
