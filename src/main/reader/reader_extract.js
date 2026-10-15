// Island Reader: a compact Readability-style extractor. Island injects this file into the page
// (DevTools Runtime.evaluate) followed by `JSON.stringify(islandExtractArticle(document, window))`;
// nothing is fetched. It relies only on tree basics (nodeType, nodeValue, tagName, childNodes,
// parentNode, getAttribute) so tests/design can run it under node against a tiny DOM.
//
// Output: {ok, title, byline, site, lang, url, published, words, truncated, content}. `content` is
// a whitelist-built tree: a string is text, an array is [tag, attrs|null, ...children]. The reader
// page rebuilds it node by node, so no page markup ever reaches Island's own document.
function islandExtractArticle(doc, win) {
  "use strict";
  var MAX_TEXT = 600000;
  var MIN_ARTICLE_TEXT = 140;
  var DROP_TAGS = {
    script: 1, style: 1, noscript: 1, template: 1, iframe: 1, frame: 1, frameset: 1, form: 1,
    button: 1, input: 1, select: 1, textarea: 1, nav: 1, aside: 1, footer: 1, svg: 1, canvas: 1,
    audio: 1, video: 1, object: 1, embed: 1, dialog: 1, menu: 1, head: 1, link: 1, meta: 1,
    map: 1, label: 1, fieldset: 1, datalist: 1, option: 1, math: 1
  };
  var UNLIKELY = new RegExp(
    "(^|[\\s_-])(comment|comments|disqus|footer|foot|menu|nav|navbox|navbar|navigation|sidebar|" +
    "sidenav|aside|related|recommended|recirculation|share|sharing|social|promo|promoted|advert|" +
    "advertisement|ad|ads|adsbygoogle|sponsor|sponsored|cookie|consent|popup|modal|newsletter|" +
    "subscribe|subscription|breadcrumb|breadcrumbs|toc|toolbar|pagination|pager|skip|sr-only|" +
    "visually-hidden|noprint|mw-editsection|catlinks|printfooter|infobox|sidebar-card|" +
    "vertical-navbox|authority-control|navigation-not-searchable|mw-jump-link|reflist-hidden|" +
    "hlist|tags|taglist|outbrain|taboola|byline-share|print-only|screen-reader-text)($|[\\s_-])", "i");
  var POSITIVE = /(^|[\s_-])(article|body|content|entry|main|page|post|text|blog|story|prose)($|[\s_-])/i;
  var NEGATIVE = /(^|[\s_-])(comment|meta|footer|footnote|sidebar|widget|share|social|promo|ad|related|tool)($|[\s_-])/i;
  var INLINE_OUT = { a: 1, em: 1, strong: 1, code: 1, sub: 1, sup: 1, br: 1 };
  var INLINE_RENAME = { b: "strong", i: "em", cite: "em", strong: "strong", em: "em", code: "code",
    tt: "code", kbd: "code", samp: "code", sub: "sub", sup: "sup", mark: "em", u: "em", small: null };
  var KEEP_BLOCKS = { p: 1, blockquote: 1, ul: 1, ol: 1, li: 1, pre: 1, figure: 1, figcaption: 1,
    table: 1, thead: 1, tbody: 1, tfoot: 1, tr: 1, th: 1, td: 1, hr: 1, dl: 1, dt: 1, dd: 1 };
  var HEADING = /^h[1-6]$/;

  function tagOf(node) { return node.nodeType === 1 ? String(node.tagName).toLowerCase() : ""; }
  function attr(node, name) {
    var value = node.getAttribute ? node.getAttribute(name) : null;
    return value == null ? "" : String(value);
  }
  function kids(node) { return node.childNodes ? Array.prototype.slice.call(node.childNodes) : []; }
  function squash(text) { return String(text).replace(/\s+/g, " "); }

  var textCache = new Map();
  var linkCache = new Map();
  function textLen(node) {
    if (node.nodeType === 3) return squash(node.nodeValue || "").trim().length;
    if (node.nodeType !== 1) return 0;
    if (textCache.has(node)) return textCache.get(node);
    var total = 0, children = kids(node);
    for (var i = 0; i < children.length; i++) total += textLen(children[i]);
    textCache.set(node, total);
    return total;
  }
  function linkLen(node) {
    if (node.nodeType !== 1) return 0;
    if (tagOf(node) === "a") return textLen(node);
    if (linkCache.has(node)) return linkCache.get(node);
    var total = 0, children = kids(node);
    for (var i = 0; i < children.length; i++) total += linkLen(children[i]);
    linkCache.set(node, total);
    return total;
  }
  function linkDensity(node) {
    var total = textLen(node);
    return total === 0 ? 0 : linkLen(node) / total;
  }
  function plainText(node) {
    if (node.nodeType === 3) return node.nodeValue || "";
    if (node.nodeType !== 1 || DROP_TAGS[tagOf(node)]) return "";
    return kids(node).map(plainText).join("");
  }
  function idClass(node) { return attr(node, "class") + " " + attr(node, "id"); }
  function isHidden(node) {
    if (node.hidden === true || node.hasAttribute && node.hasAttribute("hidden")) return true;
    if (attr(node, "aria-hidden") === "true" && !HEADING.test(tagOf(node))) return true;
    if (/display\s*:\s*none|visibility\s*:\s*hidden/i.test(attr(node, "style"))) return true;
    return false;
  }
  function isUnlikely(node) {
    var label = idClass(node);
    return label.length > 1 && UNLIKELY.test(label) && !POSITIVE.test(label) ||
      /^(navigation|banner|complementary|contentinfo|search|dialog)$/.test(attr(node, "role"));
  }
  function inUnlikelyChain(node) {
    for (var cur = node, depth = 0; cur && cur.nodeType === 1 && depth < 6; cur = cur.parentNode, depth++) {
      var tag = tagOf(cur);
      if (tag === "body" || tag === "html") return false;
      if (DROP_TAGS[tag] || isHidden(cur) || isUnlikely(cur)) return true;
    }
    return false;
  }

  // --- Candidate selection ---------------------------------------------------------------------
  var body = doc.body || doc.documentElement;
  var scores = new Map();
  function baseScore(node) {
    var tag = tagOf(node), score = 0;
    if (tag === "article" || tag === "main") score += 10;
    else if (tag === "div") score += 5;
    else if (tag === "blockquote" || tag === "pre" || tag === "td") score += 3;
    else if (tag === "form" || tag === "ol" || tag === "ul" || tag === "dl" || tag === "li") score -= 3;
    else if (HEADING.test(tag) || tag === "th") score -= 5;
    var label = idClass(node);
    if (POSITIVE.test(label)) score += 25;
    if (NEGATIVE.test(label)) score -= 25;
    return score;
  }
  function credit(node, amount) {
    if (!node || node.nodeType !== 1) return;
    if (!scores.has(node)) scores.set(node, baseScore(node));
    scores.set(node, scores.get(node) + amount);
  }
  function walkParagraphs(node, visit) {
    var children = kids(node);
    for (var i = 0; i < children.length; i++) {
      var child = children[i];
      if (child.nodeType !== 1 || DROP_TAGS[tagOf(child)] || isHidden(child)) continue;
      var tag = tagOf(child);
      if (tag === "p" || tag === "pre" || tag === "td" || tag === "blockquote") visit(child);
      walkParagraphs(child, visit);
    }
  }
  walkParagraphs(body, function (para) {
    var length = textLen(para);
    if (length < 25 || inUnlikelyChain(para)) return;
    var text = plainText(para);
    var commas = (text.match(/[,\u3001\uFF0C]/g) || []).length;
    var amount = 1 + commas + Math.min(Math.floor(length / 100), 3);
    var parent = para.parentNode, level = 0;
    while (parent && parent.nodeType === 1 && level < 5) {
      var tag = tagOf(parent);
      if (tag === "body" || tag === "html") break;
      credit(parent, level === 0 ? amount : amount / (level === 1 ? 2 : level * 3));
      parent = parent.parentNode;
      level++;
    }
  });
  function adjusted(node) { return (scores.get(node) || 0) * (1 - linkDensity(node)); }

  var top = null, topScore = 0;
  scores.forEach(function (_, node) {
    var value = adjusted(node);
    if (top === null || value > topScore) { top = node; topScore = value; }
  });

  var roots = [];
  if (top) {
    // An article split across sibling blocks: keep the siblings that score close to the winner.
    var parent = top.parentNode;
    var threshold = Math.max(10, topScore * 0.2);
    var siblings = parent ? kids(parent) : [top];
    for (var s = 0; s < siblings.length; s++) {
      var sibling = siblings[s];
      if (sibling === top) { roots.push(sibling); continue; }
      if (sibling.nodeType !== 1 || DROP_TAGS[tagOf(sibling)] || isHidden(sibling)) continue;
      if (scores.has(sibling) && adjusted(sibling) >= threshold && !isUnlikely(sibling)) {
        roots.push(sibling);
      } else if (tagOf(sibling) === "p" && textLen(sibling) > 80 && linkDensity(sibling) < 0.25) {
        roots.push(sibling);
      }
    }
  }
  var rootText = 0;
  roots.forEach(function (root) { rootText += textLen(root); });
  if (rootText < MIN_ARTICLE_TEXT) {
    // Fall back to the semantic containers, then to the whole body.
    var fallback = null;
    var semantic = ["article", "main"];
    (function find(node) {
      var children = kids(node);
      for (var i = 0; i < children.length && !fallback; i++) {
        var child = children[i];
        if (child.nodeType !== 1) continue;
        if (semantic.indexOf(tagOf(child)) >= 0 && textLen(child) >= MIN_ARTICLE_TEXT) fallback = child;
        else find(child);
      }
    })(body);
    roots = [fallback || body];
    rootText = textLen(roots[0]);
  }
  if (rootText < MIN_ARTICLE_TEXT) return { ok: false, reason: "no_article" };

  // --- Metadata --------------------------------------------------------------------------------
  function metaContent(keys) {
    var found = "";
    (function walk(node) {
      if (found) return;
      var children = kids(node);
      for (var i = 0; i < children.length && !found; i++) {
        var child = children[i];
        if (child.nodeType !== 1) continue;
        if (tagOf(child) === "meta") {
          var key = (attr(child, "property") || attr(child, "name") || attr(child, "itemprop")).toLowerCase();
          if (keys.indexOf(key) >= 0 && attr(child, "content").trim()) found = attr(child, "content").trim();
        } else if (tagOf(child) === "head" || tagOf(child) === "html") walk(child);
      }
    })(doc.documentElement);
    return found;
  }
  function firstByTag(node, tag) {
    var children = kids(node);
    for (var i = 0; i < children.length; i++) {
      var child = children[i];
      if (child.nodeType !== 1 || DROP_TAGS[tagOf(child)]) continue;
      if (tagOf(child) === tag) return child;
      var hit = firstByTag(child, tag);
      if (hit) return hit;
    }
    return null;
  }
  function allByTag(node, tag, out) {
    var children = kids(node);
    for (var i = 0; i < children.length; i++) {
      var child = children[i];
      if (child.nodeType !== 1) continue;
      if (tagOf(child) === tag) out.push(child);
      allByTag(child, tag, out);
    }
    return out;
  }
  function findByLabel(node, pattern) {
    var children = kids(node);
    for (var i = 0; i < children.length; i++) {
      var child = children[i];
      if (child.nodeType !== 1 || DROP_TAGS[tagOf(child)]) continue;
      var label = attr(child, "rel") + " " + attr(child, "itemprop") + " " + attr(child, "class");
      if (pattern.test(label) && textLen(child) > 0 && textLen(child) < 120) return child;
      var hit = findByLabel(child, pattern);
      if (hit) return hit;
    }
    return null;
  }
  var pageTitle = squash(doc.title || metaContent(["og:title", "twitter:title"]) || "").trim();
  var metaTitle = squash(metaContent(["og:title", "twitter:title"])).trim();
  var title = "";
  var heads = allByTag(body, "h1", []);
  for (var h = 0; h < heads.length && !title; h++) {
    var headText = squash(plainText(heads[h])).trim();
    var lowered = headText.toLowerCase();
    if (headText.length < 3 || headText.length > 220 || inUnlikelyChain(heads[h])) continue;
    if ((pageTitle && pageTitle.toLowerCase().indexOf(lowered) >= 0) ||
        (metaTitle && metaTitle.toLowerCase().indexOf(lowered) >= 0) ||
        (metaTitle && lowered.indexOf(metaTitle.toLowerCase()) >= 0)) title = headText;
  }
  if (!title && heads.length) {
    var firstHead = squash(plainText(heads[0])).trim();
    if (firstHead.length >= 3 && firstHead.length <= 220) title = firstHead;
  }
  if (!title) title = metaTitle || pageTitle;
  var byline = squash(metaContent(["author", "article:author", "og:article:author"])).trim();
  if (!byline) {
    var bylineNode = findByLabel(body, /(^|[\s_-])(author|byline)($|[\s_-])/i);
    byline = bylineNode ? squash(plainText(bylineNode)).trim().replace(/^by\s+/i, "") : "";
  }
  if (/^https?:\/\//i.test(byline)) byline = "";
  var site = squash(metaContent(["og:site_name", "application-name"])).trim();
  var published = metaContent(["article:published_time", "datepublished", "date", "og:published_time"]).slice(0, 10);
  var pageUrl = (doc.location && doc.location.href) || (win && win.location && win.location.href) || "";
  var lang = attr(doc.documentElement, "lang");

  // --- Serialization ---------------------------------------------------------------------------
  var budget = MAX_TEXT, truncated = false, words = 0;
  function absolute(href) {
    try {
      var resolved = new URL(href, pageUrl || undefined);
      return resolved.protocol === "http:" || resolved.protocol === "https:" ? resolved.href : "";
    } catch (error) { return ""; }
  }
  function pushText(out, text) {
    if (!text) return;
    if (budget <= 0) { truncated = true; return; }
    if (text.length > budget) { text = text.slice(0, budget); truncated = true; }
    budget -= text.length;
    words += (text.match(/\S+/g) || []).length;
    out.push(text);
  }
  function hasContent(items) {
    for (var i = 0; i < items.length; i++) {
      var item = items[i];
      if (typeof item === "string") { if (item.trim()) return true; }
      else if (item[0] === "img" || item[0] === "hr") return true;
      else if (hasContent(item.slice(2))) return true;
    }
    return false;
  }
  function isInline(item) { return typeof item === "string" || !!INLINE_OUT[item[0]]; }
  // Runs of inline items between blocks become paragraphs (a bare <div>text</div> is a paragraph).
  function paragraphize(items) {
    var out = [], run = [];
    function flush() {
      if (run.length && hasContent(run)) {
        if (typeof run[0] === "string") run[0] = run[0].replace(/^\s+/, "");
        var last = run.length - 1;
        if (typeof run[last] === "string") run[last] = run[last].replace(/\s+$/, "");
        out.push(["p", null].concat(run));
      }
      run = [];
    }
    for (var i = 0; i < items.length; i++) {
      if (isInline(items[i])) run.push(items[i]);
      else { flush(); out.push(items[i]); }
    }
    flush();
    return out;
  }
  function imageSource(node) {
    var src = attr(node, "src") || attr(node, "data-src") || attr(node, "data-original") ||
      attr(node, "data-lazy-src");
    var srcset = attr(node, "srcset") || attr(node, "data-srcset");
    if ((!src || /^data:/i.test(src)) && srcset) src = srcset.split(",")[0].trim().split(/\s+/)[0];
    if (node.currentSrc && /^https?:/i.test(node.currentSrc)) src = node.currentSrc;
    return absolute(src);
  }
  function serializeChildren(node, preformatted) {
    var out = [], children = kids(node);
    for (var i = 0; i < children.length; i++) {
      var items = serialize(children[i], preformatted);
      for (var j = 0; j < items.length; j++) out.push(items[j]);
    }
    return out;
  }
  function serialize(node, preformatted) {
    if (budget <= 0) { truncated = true; return []; }
    if (node.nodeType === 3) {
      var out = [], value = node.nodeValue || "";
      pushText(out, preformatted ? value : squash(value));
      return out;
    }
    if (node.nodeType !== 1) return [];
    var tag = tagOf(node);
    if (DROP_TAGS[tag] || isHidden(node)) return [];
    if (isUnlikely(node) && roots.indexOf(node) < 0) return [];
    if (tag === "sup" && /(^|[\s_-])reference($|[\s_-])/.test(attr(node, "class"))) return [];
    if (tag === "img") {
      var width = parseInt(attr(node, "width"), 10), height = parseInt(attr(node, "height"), 10);
      if ((width > 0 && width <= 40) || (height > 0 && height <= 40)) return [];
      var source = imageSource(node);
      if (!source) return [];
      var attrs = { src: source };
      var alt = squash(attr(node, "alt")).trim();
      if (alt) attrs.alt = alt.slice(0, 300);
      return [["img", attrs]];
    }
    if (tag === "br") return [["br", null]];
    if (tag === "hr") return [["hr", null]];
    if (tag === "a") {
      var href = absolute(attr(node, "href"));
      var anchorKids = serializeChildren(node, false);
      if (!href || /^#/.test(attr(node, "href")) || !hasContent(anchorKids)) return anchorKids;
      return [["a", { href: href }].concat(anchorKids)];
    }
    if (Object.prototype.hasOwnProperty.call(INLINE_RENAME, tag)) {
      var renamed = INLINE_RENAME[tag];
      var inner = serializeChildren(node, tag === "code" ? preformatted : false);
      if (!renamed || !hasContent(inner)) return inner;
      return [[renamed, null].concat(inner)];
    }
    if (tag === "picture") return serializeChildren(node, false);
    if (HEADING.test(tag)) {
      var headingKids = serializeChildren(node, false);
      if (!hasContent(headingKids)) return [];
      var level = Math.max(2, Math.min(6, parseInt(tag.slice(1), 10)));
      return [["h" + level, null].concat(headingKids)];
    }
    if (tag === "pre") {
      var literal = [];
      pushText(literal, plainText(node).replace(/\n+$/, ""));
      return literal.length ? [["pre", null].concat(literal)] : [];
    }
    if (KEEP_BLOCKS[tag]) {
      if ((tag === "ul" || tag === "ol" || tag === "table") && textLen(node) < 1500 &&
          linkDensity(node) > 0.65) return [];
      var inside = serializeChildren(node, false);
      if (tag === "li" || tag === "blockquote" || tag === "figure" || tag === "td" || tag === "th" ||
          tag === "dd") {
        var hasBlock = inside.some(function (item) { return !isInline(item); });
        if (hasBlock) inside = paragraphize(inside);
      }
      if (!hasContent(inside)) return [];
      return [[tag, null].concat(inside)];
    }
    // Everything else is a container: unwrap it, keeping its blocks and wrapping stray text.
    if (textLen(node) < 400 && linkDensity(node) > 0.65 && node !== body &&
        roots.indexOf(node) < 0) return [];
    var contents = serializeChildren(node, preformatted);
    var blockLevel = contents.some(function (item) { return !isInline(item); });
    var isRun = !blockLevel && (tag === "span" || tag === "font" || tag === "small" || tag === "label");
    return isRun ? contents : paragraphize(contents);
  }

  var content = [];
  roots.forEach(function (root) {
    var items = tagOf(root) && root.nodeType === 1 ? serialize(root, false) : [];
    items.forEach(function (item) { content.push(item); });
  });
  content = paragraphize(content);
  // Drop the leading copy of the title, and headings left dangling by removed sections.
  if (content.length && content[0][0] === "h2" &&
      squash(flatText(content[0])).trim().toLowerCase() === title.toLowerCase()) content.shift();
  function flatText(item) {
    if (typeof item === "string") return item;
    return item.slice(2).map(flatText).join("");
  }
  var cleaned = [];
  for (var c = 0; c < content.length; c++) {
    var entry = content[c];
    if (HEADING.test(entry[0])) {
      var next = content[c + 1];
      if (!next || HEADING.test(next[0])) continue;
    }
    cleaned.push(entry);
  }
  if (!cleaned.length || words < 20) return { ok: false, reason: "no_article" };
  return {
    ok: true, title: title, byline: byline.slice(0, 120), site: site.slice(0, 80), lang: lang,
    url: pageUrl, published: published, words: words, truncated: truncated, content: cleaned
  };
}
