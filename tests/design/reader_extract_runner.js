// Reads [{url, html}] on stdin, runs src/main/reader/reader_extract.js against each document with
// the tiny DOM shim, and writes the extraction results as JSON on stdout.
"use strict";
const fs = require("fs");
const path = require("path");
const { parse } = require("./reader_dom_shim.js");

const source = fs.readFileSync(
  path.join(__dirname, "..", "..", "src", "main", "reader", "reader_extract.js"), "utf8");
const extract = new Function(source + "\nreturn islandExtractArticle;")();

let input = "";
process.stdin.on("data", (chunk) => { input += chunk; });
process.stdin.on("end", () => {
  const results = JSON.parse(input).map(({ url, html }) => {
    const doc = parse(html, url);
    // The page contract: the injected expression returns a JSON string.
    return JSON.parse(JSON.stringify(extract(doc, { location: doc.location })));
  });
  process.stdout.write(JSON.stringify(results));
});
