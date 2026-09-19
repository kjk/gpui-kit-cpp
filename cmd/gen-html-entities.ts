import { readFileSync, writeFileSync } from "node:fs";
import { resolve } from "node:path";

const root = resolve(import.meta.dir, "..");
const src = readFileSync(resolve(root, "src/markdown/constant.cpp"), "utf8").split(/\r?\n/);

function sliceUntilNext(startPred: (l: string) => boolean, endPred: (l: string, i: number) => boolean): string {
  const a = src.findIndex(startPred);
  const b = src.findIndex((l, i) => i > a && endPred(l, i));
  return src.slice(a, b).join("\n").trimEnd();
}

const names = sliceUntilNext(
  (l) => l.startsWith("const char kCharacterReferenceNames[]"),
  (l) => l.startsWith("const char kCharacterReferenceValues[]"),
);
const values = sliceUntilNext(
  (l) => l.startsWith("const char kCharacterReferenceValues[]"),
  (l) => l.startsWith("const CharacterReference kCharacterReferences"),
);
const refs = (() => {
  const a = src.findIndex((l) => l.startsWith("const CharacterReference kCharacterReferences"));
  const b = src.findIndex((l, i) => i > a && l.trim() === "};");
  return src.slice(a, b + 1).join("\n");
})();

const out = `/* WHATWG named character references, the same 2125-name table markdown
   holds in constant.cpp. html5ever cannot include markdown (crate isolation),
   so the bytes live here and NamedEntity binary-searches them. */

#include "html5ever/html5ever.h"

namespace html5ever {

using base::Str;
using base::StrCmp;

${names.replace("kCharacterReferenceNames", "kNamedRefNames")}

${values.replace("kCharacterReferenceValues", "kNamedRefValues")}

struct NamedRef {
    int32_t nameOff;
    int32_t valueOff;
};

${refs.replace("const CharacterReference kCharacterReferences", "const NamedRef kNamedRefs")}

Str NamedEntity(Str name) {
    int32_t lo = 0;
    int32_t hi = 2125 - 1;
    while (lo <= hi) {
        int32_t mid = (lo + hi) / 2;
        const char* candidate = kNamedRefNames + kNamedRefs[mid].nameOff;
        int cmp = StrCmp(Str(candidate), name);
        if (cmp < 0) {
            lo = mid + 1;
        } else if (cmp > 0) {
            hi = mid - 1;
        } else {
            const char* value = kNamedRefValues + kNamedRefs[mid].valueOff;
            return Str(value);
        }
    }
    return {};
}

} // namespace html5ever
`;

writeFileSync(resolve(root, "src/html5ever/entities.cpp"), out);
console.log("wrote src/html5ever/entities.cpp");
