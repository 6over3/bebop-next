export class IndentedStringBuilder {
  private readonly chunks: string[] = [];

  constructor(private spaces = 0) {}

  append(text: string): this {
    const indent = " ".repeat(this.spaces);
    if (!text.includes("\n") && !text.includes("\r")) {
      this.chunks.push(`${indent}${text}`.trimEnd());
      return this;
    }
    const lines = text.split(/\r\n|\n|\r/u);
    this.chunks.push(lines.map((line) => `${indent}${line}`.trimEnd()).join("\n").trimEnd());
    return this;
  }

  line(text = ""): this {
    if (text.length === 0) {
      this.chunks.push("\n");
      return this;
    }
    if (!text.includes("\n") && !text.includes("\r")) {
      this.chunks.push(`${" ".repeat(this.spaces)}${text.trimEnd()}\n`);
      return this;
    }
    this.append(text);
    this.chunks.push("\n");
    return this;
  }

  indented(fn: (builder: this) => void, spaces = 2): this {
    const previous = this.spaces;
    this.spaces = Math.max(0, previous + spaces);
    try {
      fn(this);
    } finally {
      this.spaces = previous;
    }
    return this;
  }

  block(openingLine: string, fn: (builder: this) => void, close = "}", open = "{"): this {
    if (openingLine.length > 0) {
      this.append(openingLine);
      this.chunks.push(` ${open}\n`);
    } else {
      this.line(open);
    }

    this.indented(fn);
    this.line(close);
    return this;
  }

  trimEnd(): this {
    const content = this.toString().trimEnd();
    this.chunks.length = 0;
    this.chunks.push(content);
    return this;
  }

  toString(): string {
    return this.chunks.join("");
  }
}
