// A tiny HTML parser and DOM for the reader-extraction tests: just the tree basics the extractor
// uses (nodeType, nodeValue, tagName, childNodes, parentNode, getAttribute, hasAttribute).
// Fixtures are well-formed snippets, so this is a tolerant tokenizer, not an HTML5 parser.
"use strict";

const VOID = new Set(["br", "hr", "img", "meta", "link", "input", "source", "wbr"]);
const RAW = new Set(["script", "style"]);
const ENTITIES = { "&amp;": "&", "&lt;": "<", "&gt;": ">", "&quot;": '"', "&#39;": "'", "&nbsp;": " " };

class Node {
  constructor(nodeType) { this.nodeType = nodeType; this.parentNode = null; this.childNodes = []; }
  append(child) { child.parentNode = this; this.childNodes.push(child); }
}
class Text extends Node {
  constructor(value) { super(3); this.nodeValue = value; }
}
class Element extends Node {
  constructor(tag, attrs) {
    super(1);
    this.tagName = tag.toUpperCase();
    this.attrs = attrs;
  }
  getAttribute(name) { return Object.prototype.hasOwnProperty.call(this.attrs, name) ? this.attrs[name] : null; }
  hasAttribute(name) { return Object.prototype.hasOwnProperty.call(this.attrs, name); }
  get hidden() { return this.hasAttribute("hidden"); }
}

function decode(text) {
  return text.replace(/&(amp|lt|gt|quot|nbsp|#39);/g, (match) => ENTITIES[match]);
}

function parseAttrs(source) {
  const attrs = {};
  const pattern = /([a-zA-Z_:][-a-zA-Z0-9_:.]*)(?:\s*=\s*(?:"([^"]*)"|'([^']*)'|([^\s"'>]+)))?/g;
  let match;
  while ((match = pattern.exec(source))) {
    attrs[match[1].toLowerCase()] = decode(match[2] ?? match[3] ?? match[4] ?? "");
  }
  return attrs;
}

function parse(html, url) {
  const root = new Element("html", {});
  const stack = [root];
  const token = /<!--[\s\S]*?-->|<\/([a-zA-Z][a-zA-Z0-9]*)\s*>|<([a-zA-Z][a-zA-Z0-9]*)((?:"[^"]*"|'[^']*'|[^>"'])*)>|([^<]+)/g;
  let match;
  while ((match = token.exec(html))) {
    const top = stack[stack.length - 1];
    if (match[4] !== undefined) {
      top.append(new Text(decode(match[4])));
    } else if (match[1]) {
      const tag = match[1].toLowerCase();
      for (let i = stack.length - 1; i > 0; i--) {
        if (stack[i].tagName.toLowerCase() === tag) { stack.length = i; break; }
      }
    } else if (match[2]) {
      const tag = match[2].toLowerCase();
      const element = new Element(tag, parseAttrs(match[3].replace(/\/\s*$/, "")));
      top.append(element);
      if (RAW.has(tag)) {
        const close = html.indexOf("</" + tag, token.lastIndex);
        const end = close < 0 ? html.length : close;
        element.append(new Text(html.slice(token.lastIndex, end)));
        token.lastIndex = end;
      } else if (!VOID.has(tag) && !/\/\s*>$/.test(match[0])) {
        stack.push(element);
      }
    }
  }
  const find = (node, tag) => {
    for (const child of node.childNodes) {
      if (child.nodeType !== 1) continue;
      if (child.tagName.toLowerCase() === tag) return child;
      const hit = find(child, tag);
      if (hit) return hit;
    }
    return null;
  };
  const titleNode = find(root, "title");
  return {
    documentElement: find(root, "html") || root,
    body: find(root, "body") || root,
    title: titleNode ? titleNode.childNodes.map((n) => n.nodeValue).join("") : "",
    location: { href: url },
  };
}

module.exports = { parse };
