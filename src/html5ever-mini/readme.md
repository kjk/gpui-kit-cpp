# html5ever-mini

`html5ever-mini` implements the public API in
[`src/html5ever/html5ever.h`](../html5ever/html5ever.h). Select it with:

```text
bun cmd/build.ts -html=mini -rel story
```

The full html5ever 0.27 port is the default. The mini parser is the former
reader-mode tokenizer from `base/text_format.cpp`, moved behind the same DOM
surface. It supports elements, attributes, comments, raw `script`/`style`
text, numeric references and the common named references. It deliberately
omits HTML5 insertion modes, implied document elements, foster parenting,
formatting-element reconstruction, foreign-content rules and the complete
named-reference table.

Unsupported malformed markup is kept readable. Incremental `ParserProcess`
concatenates chunks and pauses after `</script>` when scripting is enabled;
`ParserFinish` parses the whole buffer. The standalone
`extras/html5ever-mini` distribution includes `base` and must not be linked
beside `gpui.cpp`.
