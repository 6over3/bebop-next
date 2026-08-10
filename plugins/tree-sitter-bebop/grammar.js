const PRIMITIVE_TYPES = [
  'bool',
  'byte',
  'uint8',
  'sbyte',
  'int8',
  'int16',
  'uint16',
  'int32',
  'uint32',
  'int64',
  'uint64',
  'int128',
  'uint128',
  'float16',
  'half',
  'float32',
  'float64',
  'bfloat16',
  'bf16',
  'string',
  'uuid',
  'guid',
  'timestamp',
  'duration',
];

const PREC = {
  OR: 1,
  AND: 2,
  SHIFT: 3,
  ADDITIVE: 4,
  UNARY: 5,
};

function commaSep1(rule) {
  return seq(rule, repeat(seq(',', rule)));
}

function commaSep(rule) {
  return optional(commaSep1(rule));
}

module.exports = grammar({
  name: 'bebop',

  word: $ => $.identifier,

  externals: $ => [
    $._lua_block_start,
    $.lua_source,
    $._lua_block_end,
  ],

  extras: $ => [
    /\s/,
    $.doc_comment,
    $.line_comment,
    $.block_comment,
  ],

  rules: {
    source_file: $ => repeat($._statement),

    _statement: $ => choice(
      $.edition_declaration,
      $.package_declaration,
      $.import_declaration,
      $.decorator,
      $.decorator_definition,
      $._definition,
    ),

    edition_declaration: $ => seq(
      'edition',
      '=',
      field('edition', $.string),
      optional(';'),
    ),

    package_declaration: $ => seq(
      'package',
      field('name', $.qualified_identifier),
      optional(';'),
    ),

    import_declaration: $ => seq(
      'import',
      field('path', $.string),
      optional(';'),
    ),

    _definition: $ => seq(
      repeat($.modifier),
      choice(
        $.struct_definition,
        $.message_definition,
        $.enum_definition,
        $.union_definition,
        $.service_definition,
        $.const_declaration,
      ),
    ),

    modifier: _ => choice('export', 'local', 'mut', 'readonly'),

    struct_definition: $ => seq(
      'struct',
      field('name', $.identifier),
      field('body', $.struct_body),
    ),

    struct_body: $ => seq('{', repeat($._struct_member), '}'),

    _struct_member: $ => choice(
      $.field_declaration,
      $.decorator,
      $._definition,
    ),

    field_declaration: $ => seq(
      field('name', $.identifier),
      ':',
      field('type', $._type),
      ';',
    ),

    message_definition: $ => seq(
      'message',
      field('name', $.identifier),
      field('body', $.message_body),
    ),

    message_body: $ => seq('{', repeat($._message_member), '}'),

    _message_member: $ => choice(
      $.tagged_field_declaration,
      $.decorator,
      $._definition,
    ),

    tagged_field_declaration: $ => seq(
      field('name', $.identifier),
      '(',
      field('tag', $.number),
      ')',
      ':',
      field('type', $._type),
      ';',
    ),

    enum_definition: $ => seq(
      'enum',
      field('name', $.identifier),
      optional(seq(':', field('type', $._type))),
      field('body', $.enum_body),
    ),

    enum_body: $ => seq(
      '{',
      repeat(choice($.enum_member, $.decorator)),
      '}',
    ),

    enum_member: $ => seq(
      field('name', $.identifier),
      '=',
      field('value', $._expression),
      ';',
    ),

    union_definition: $ => seq(
      'union',
      field('name', $.identifier),
      field('body', $.union_body),
    ),

    union_body: $ => seq(
      '{',
      repeat(choice($.union_branch, $.decorator, $._definition)),
      '}',
    ),

    union_branch: $ => seq(
      repeat($.modifier),
      field('name', $.identifier),
      '(',
      field('discriminator', $.number),
      ')',
      ':',
      field('value', choice($.inline_struct, $.inline_message, $._type)),
      ';',
    ),

    inline_struct: $ => seq(
      optional($.modifier),
      $.struct_body,
    ),

    inline_message: $ => seq(
      'message',
      $.message_body,
    ),

    service_definition: $ => seq(
      'service',
      field('name', $.identifier),
      optional($.with_clause),
      field('body', $.service_body),
    ),

    with_clause: $ => seq(
      'with',
      commaSep1(field('service', $.qualified_identifier)),
    ),

    service_body: $ => seq(
      '{',
      repeat(choice($.method_declaration, $.decorator)),
      '}',
    ),

    method_declaration: $ => seq(
      field('name', $.identifier),
      '(',
      optional(field('request_stream', $.stream_modifier)),
      field('request', $._type),
      ')',
      ':',
      optional(field('response_stream', $.stream_modifier)),
      field('response', $._type),
      ';',
    ),

    stream_modifier: _ => 'stream',

    const_declaration: $ => seq(
      'const',
      field('type', $._type),
      field('name', $.identifier),
      '=',
      field('value', $._literal),
      ';',
    ),

    decorator: $ => seq(
      '@',
      field('name', $.qualified_identifier),
      optional(field('arguments', $.decorator_arguments)),
    ),

    decorator_arguments: $ => seq(
      '(',
      commaSep($.decorator_argument),
      ')',
    ),

    decorator_argument: $ => seq(
      optional(seq(field('name', $.identifier), ':')),
      field('value', $._literal),
    ),

    decorator_definition: $ => seq(
      '#',
      'decorator',
      '(',
      field('name', $.identifier),
      ')',
      field('body', $.decorator_body),
    ),

    decorator_body: $ => seq(
      '{',
      repeat($._decorator_body_item),
      '}',
    ),

    _decorator_body_item: $ => choice(
      $.targets_setting,
      $.multiple_setting,
      $.param_declaration,
      $.validate_block,
      $.export_block,
    ),

    targets_setting: $ => seq(
      'targets',
      '=',
      field('value', $.target_expression),
    ),

    target_expression: $ => seq(
      $.target,
      repeat(seq('|', $.target)),
    ),

    target: $ => $.identifier,

    multiple_setting: $ => seq(
      'multiple',
      '=',
      field('value', $.boolean),
    ),

    param_declaration: $ => seq(
      'param',
      field('name', $.identifier),
      field('modifier', choice('!', '?')),
      ':',
      field('type', $._type),
      optional(seq('=', field('default', $._literal))),
      optional(seq('in', field('allowed', $.allowed_values))),
    ),

    allowed_values: $ => seq('[', commaSep($._literal), ']'),

    validate_block: $ => seq('validate', field('code', $.lua_block)),

    export_block: $ => seq('export', field('code', $.lua_block)),

    lua_block: $ => seq(
      alias($._lua_block_start, '[['),
      optional($.lua_source),
      alias($._lua_block_end, ']]'),
    ),

    _type: $ => choice(
      $.primitive_type,
      $.map_type,
      $.array_type,
      alias($.qualified_identifier, $.type_identifier),
    ),

    primitive_type: _ => choice(...PRIMITIVE_TYPES),

    map_type: $ => seq(
      'map',
      '[',
      field('key', $._type),
      ',',
      field('value', $._type),
      ']',
    ),

    array_type: $ => prec.left(seq(
      field('element', $._type),
      '[',
      optional(field('size', $.number)),
      ']',
    )),

    _expression: $ => choice(
      $.number,
      $.identifier,
      $.unary_expression,
      $.binary_expression,
      $.parenthesized_expression,
    ),

    unary_expression: $ => prec(PREC.UNARY, seq(
      field('operator', choice('~', '-')),
      field('operand', $._expression),
    )),

    binary_expression: $ => choice(
      ...[
        ['|', PREC.OR],
        ['&', PREC.AND],
        ['<<', PREC.SHIFT],
        ['>>', PREC.SHIFT],
        ['+', PREC.ADDITIVE],
        ['-', PREC.ADDITIVE],
      ].map(([operator, precedence]) =>
        prec.left(precedence, seq(
          field('left', $._expression),
          field('operator', operator),
          field('right', $._expression),
        )),
      ),
    ),

    parenthesized_expression: $ => seq('(', $._expression, ')'),

    _literal: $ => choice(
      $.string,
      $.bytes_string,
      $.boolean,
      $.numeric_literal,
    ),

    boolean: _ => choice('true', 'false'),

    numeric_literal: $ => seq(
      optional('-'),
      choice($.number, $.infinity, $.not_a_number),
    ),

    infinity: _ => 'inf',
    not_a_number: _ => 'nan',

    number: _ => token(choice(
      /0[xX][0-9a-fA-F]+/,
      /[0-9]+(\.[0-9]+)?([eE][+-]?[0-9]+)?/,
    )),

    string: $ => choice(
      seq('"', repeat($._double_quoted_content), '"'),
      seq("'", repeat($._single_quoted_content), "'"),
    ),

    bytes_string: $ => choice(
      seq(alias(token(seq('b', '"')), 'b"'), repeat($._double_quoted_content), '"'),
      seq(alias(token(seq('b', "'")), "b'"), repeat($._single_quoted_content), "'"),
    ),

    _double_quoted_content: $ => choice(
      $.escape_sequence,
      alias(token.immediate('""'), $.escape_sequence),
      $.env_variable,
      token.immediate(prec(1, /[^"\\$]+/)),
      token.immediate('$'),
    ),

    _single_quoted_content: $ => choice(
      $.escape_sequence,
      alias(token.immediate("''"), $.escape_sequence),
      $.env_variable,
      token.immediate(prec(1, /[^'\\$]+/)),
      token.immediate('$'),
    ),

    escape_sequence: _ => token.immediate(
      /\\(u\{[0-9a-fA-F]{1,6}\}|x[0-9a-fA-F]{2}|.)/,
    ),

    env_variable: _ => token.immediate(/\$\([A-Za-z_][A-Za-z0-9_]*\)/),

    qualified_identifier: $ => seq(
      $.identifier,
      repeat(seq('.', $.identifier)),
    ),

    identifier: _ => /[A-Za-z_][A-Za-z0-9_]*/,

    doc_comment: _ => token(prec(2, /\/\/\/[^\n]*/)),

    line_comment: _ => token(/\/\/[^\n]*/),

    block_comment: _ => token(/\/\*[^*]*\*+([^/*][^*]*\*+)*\//),
  },
});
