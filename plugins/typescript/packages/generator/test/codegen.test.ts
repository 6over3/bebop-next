import { describe, expect, test } from "vitest";
import { IndentedStringBuilder } from "../src/indented-string-builder";

describe("IndentedStringBuilder", () => {
  test("writes TypeScript with closures for blocks", () => {
    const builder = new IndentedStringBuilder();

    builder.line(`import { BebopReader, BebopWriter } from "@bebop/runtime";`);
    builder.line();
    builder.line("export type Song = {");
    builder.indented((type) => {
      type.line("title: string;");
      type.line("samples: Float32Array;");
    });
    builder.line("};");
    builder.line();
    builder.block("export const Song =", (object) => {
      object.block("read(reader: BebopReader): Song", (read) => {
        read.line("return {");
        read.indented((value) => {
          value.line("title: reader.readString(),");
          value.line("samples: reader.readFloat32Array(),");
        });
        read.line("};");
      }, "},");
      object.block("write(writer: BebopWriter, value: Song): void", (write) => {
        write.line("writer.writeString(value.title);");
        write.line("writer.writeFloat32Array(value.samples);");
      }, "},");
    }, "} satisfies BebopCodec<Song>;");

    expect(builder.toString()).toBe(`import { BebopReader, BebopWriter } from "@bebop/runtime";

export type Song = {
  title: string;
  samples: Float32Array;
};

export const Song = {
  read(reader: BebopReader): Song {
    return {
      title: reader.readString(),
      samples: reader.readFloat32Array(),
    };
  },
  write(writer: BebopWriter, value: Song): void {
    writer.writeString(value.title);
    writer.writeFloat32Array(value.samples);
  },
} satisfies BebopCodec<Song>;
`);
  });

  test("supports mid-line and end-line appends", () => {
    const builder = new IndentedStringBuilder();

    builder.append("const answer =");
    builder.appendMid(" 42");
    builder.appendEnd(";");

    expect(builder.toString()).toBe("const answer = 42;\n");
  });

  test("trims accumulated content", () => {
    const builder = new IndentedStringBuilder();

    builder.line();
    builder.line("value");
    builder.line();
    builder.trim();

    expect(builder.toString()).toBe("value");
  });
});
