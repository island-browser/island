  // --- Markdown (BEGIN MARKDOWN) ---
  // A small, self-contained renderer for agent replies. Safety rules, pinned by
  // tests/design/test_local_pages.py: every character of model text goes
  // through esc() before it is emitted; only the tags generated here appear;
  // links carry rel="noopener noreferrer" and only http, https and mailto hrefs
  // survive (anything else is shown as plain text); nothing is fetched.
  function esc(text) {
    return String(text).replace(/&/g, "&amp;").replace(/</g, "&lt;").replace(/>/g, "&gt;")
      .replace(/"/g, "&quot;").replace(/'/g, "&#39;");
  }

  function safeHref(url) {
    url = String(url).trim();
    if (!/^(https?:\/\/|mailto:)[^\s\u0000-\u001f\u007f]+$/i.test(url)) return null;
    return url;
  }

  function linkHtml(href, labelHtml) {
    return '<a href="' + esc(href) + '" rel="noopener noreferrer" title="' + esc(href) + '">' + labelHtml + "</a>";
  }

  var INLINE_SOURCE = "\\\\([\\\\`*_{}\\[\\]()#+\\-.!|~<>])" +                    // 1 escaped character
    "|(`+)([\\s\\S]*?[^`])\\2(?!`)" +                                              // 2,3 code span
    "|\\[((?:[^\\[\\]]|\\[[^\\]]*\\])*)\\]\\(\\s*<?((?:[^\\s()<>]|\\([^\\s()<>]*\\))+)>?(?:\\s+\"[^\"]*\")?\\s*\\)" + // 4,5 link
    "|<((?:https?:\\/\\/|mailto:)[^\\s<>]+)>" +                                    // 6 <autolink>
    "|(https?:\\/\\/[^\\s<>]*[^\\s<>.,;:!?'\")\\]])" +                             // 7 bare url
    "|\\*\\*(?=\\S)([\\s\\S]*?\\S)\\*\\*" +                                         // 8 bold
    "|__(?=\\S)([\\s\\S]*?\\S)__" +                                                 // 9 bold
    "|~~(?=\\S)([\\s\\S]*?\\S)~~" +                                                 // 10 strikethrough
    "|\\*(?=[^\\s*])([\\s\\S]*?[^\\s*])\\*" +                                       // 11 italic
    "|\\b_(?=[^\\s_])([\\s\\S]*?[^\\s_])_\\b";                                      // 12 italic

  function inline(text, noLinks) {
    var re = new RegExp(INLINE_SOURCE, "g");
    var out = "", last = 0, m;
    while ((m = re.exec(text)) !== null) {
      out += esc(text.slice(last, m.index));
      last = m.index + m[0].length;
      if (m[1] !== undefined) {
        out += esc(m[1]);
      } else if (m[2] !== undefined) {
        var code = m[3];
        if (code.length > 2 && code.charAt(0) === " " && code.charAt(code.length - 1) === " ") code = code.slice(1, -1);
        out += "<code>" + esc(code) + "</code>";
      } else if (m[4] !== undefined) {
        var href = safeHref(m[5]);
        out += href && !noLinks ? linkHtml(href, inline(m[4], true)) : inline(m[4], true);
      } else if (m[6] !== undefined || m[7] !== undefined) {
        var url = m[6] !== undefined ? m[6] : m[7];
        var safe = safeHref(url);
        out += safe && !noLinks ? linkHtml(safe, esc(url)) : esc(url);
      } else if (m[8] !== undefined || m[9] !== undefined) {
        out += "<strong>" + inline(m[8] !== undefined ? m[8] : m[9], noLinks) + "</strong>";
      } else if (m[10] !== undefined) {
        out += "<del>" + inline(m[10], noLinks) + "</del>";
      } else {
        out += "<em>" + inline(m[11] !== undefined ? m[11] : m[12], noLinks) + "</em>";
      }
    }
    return out + esc(text.slice(last));
  }

  var FENCE_RE = /^\s{0,3}(`{3,}|~{3,})[^`]*$/;
  var HEADING_RE = /^\s{0,3}(#{1,6})\s+(.*?)(?:\s+#+)?\s*$/;
  var HR_RE = /^\s{0,3}([-*_])(?:\s*\1){2,}\s*$/;
  var QUOTE_RE = /^\s{0,3}>\s?/;
  var LIST_RE = /^(\s*)([-*+\u2022]|\d{1,9}[.)])\s+(.*)$/;
  var TABLE_SEP_RE = /^\s*\|?\s*:?-+:?\s*(?:\|\s*:?-+:?\s*)*\|?\s*$/;

  function leading(line) { return /^\s*/.exec(line)[0].length; }

  function splitRow(line) {
    var text = line.trim();
    if (text.charAt(0) === "|") text = text.slice(1);
    if (text.charAt(text.length - 1) === "|" && text.charAt(text.length - 2) !== "\\") text = text.slice(0, -1);
    var cells = [], cell = "";
    for (var i = 0; i < text.length; i++) {
      var ch = text.charAt(i);
      if (ch === "\\" && text.charAt(i + 1) === "|") { cell += "|"; i++; }
      else if (ch === "|") { cells.push(cell.trim()); cell = ""; }
      else cell += ch;
    }
    cells.push(cell.trim());
    return cells;
  }

  function isTableStart(lines, i) {
    return i + 1 < lines.length && lines[i].indexOf("|") >= 0 && lines[i + 1].indexOf("-") >= 0 &&
      TABLE_SEP_RE.test(lines[i + 1]) && splitRow(lines[i]).length === splitRow(lines[i + 1]).length;
  }

  function startsBlock(lines, i) {
    var line = lines[i];
    return FENCE_RE.test(line) || HEADING_RE.test(line) || HR_RE.test(line) || QUOTE_RE.test(line) ||
      LIST_RE.test(line) || isTableStart(lines, i);
  }

  function renderList(lines, i, indent, out) {
    var first = LIST_RE.exec(lines[i]);
    var ordered = /\d/.test(first[2].charAt(0));
    var start = ordered ? parseInt(first[2], 10) : 1;
    var items = [];
    while (i < lines.length) {
      var m = LIST_RE.exec(lines[i]);
      if (!m || m[1].length !== indent || /\d/.test(m[2].charAt(0)) !== ordered) break;
      var item = [m[3]];
      i++;
      while (i < lines.length) {
        var line = lines[i];
        if (!line.trim()) {
          var j = i + 1;
          while (j < lines.length && !lines[j].trim()) j++;
          if (j < lines.length && leading(lines[j]) > indent) {
            for (var k = i; k < j; k++) item.push("");
            i = j;
            continue;
          }
          break;
        }
        if (leading(line) <= indent) break;
        item.push(line.slice(Math.min(leading(line), indent + 2)));
        i++;
      }
      items.push(item);
    }
    var tag = ordered ? "ol" : "ul";
    var html = "<" + tag + (ordered && start !== 1 && isFinite(start) ? ' start="' + start + '"' : "") + ">";
    items.forEach(function (item) {
      html += "<li>" + inline(item[0]) + (item.length > 1 ? blocks(item.slice(1)) : "") + "</li>";
    });
    out.push(html + "</" + tag + ">");
    return i;
  }

  function renderTable(lines, i, out) {
    var head = splitRow(lines[i]);
    var aligns = splitRow(lines[i + 1]).map(function (cell) {
      var left = cell.charAt(0) === ":", right = cell.charAt(cell.length - 1) === ":";
      return left && right ? "center" : right ? "right" : left ? "left" : "";
    });
    var cell = function (tag, text, index) {
      return "<" + tag + (aligns[index] ? ' style="text-align:' + aligns[index] + '"' : "") + ">" + inline(text) + "</" + tag + ">";
    };
    var html = '<div class="table-wrap"><table><thead><tr>' +
      head.map(function (text, index) { return cell("th", text, index); }).join("") + "</tr></thead><tbody>";
    i += 2;
    while (i < lines.length && lines[i].trim() && lines[i].indexOf("|") >= 0) {
      var cells = splitRow(lines[i]);
      html += "<tr>" + head.map(function (_, index) { return cell("td", cells[index] || "", index); }).join("") + "</tr>";
      i++;
    }
    out.push(html + "</tbody></table></div>");
    return i;
  }

  function isFenceClose(line, marker) {
    var text = line.trim();
    return text.length >= marker.length && text.split(marker.charAt(0)).join("") === "";
  }

  function blocks(lines) {
    var out = [], i = 0, paragraph = [];
    function flush() {
      if (paragraph.length) out.push("<p>" + inline(paragraph.join("\n")).replace(/\n/g, "<br>") + "</p>");
      paragraph = [];
    }
    while (i < lines.length) {
      var line = lines[i], m;
      if (!line.trim()) { flush(); i++; continue; }
      if ((m = FENCE_RE.exec(line)) !== null) {
        flush();
        var marker = m[1], code = [];
        i++;
        while (i < lines.length && !isFenceClose(lines[i], marker)) { code.push(lines[i]); i++; }
        i++;
        out.push("<pre><code>" + esc(code.join("\n")) + "</code></pre>");
      } else if ((m = HEADING_RE.exec(line)) !== null) {
        flush();
        out.push("<h" + m[1].length + ">" + inline(m[2]) + "</h" + m[1].length + ">");
        i++;
      } else if (HR_RE.test(line)) {
        flush();
        out.push("<hr>");
        i++;
      } else if (QUOTE_RE.test(line)) {
        flush();
        var quote = [];
        while (i < lines.length && QUOTE_RE.test(lines[i])) { quote.push(lines[i].replace(QUOTE_RE, "")); i++; }
        out.push("<blockquote>" + blocks(quote) + "</blockquote>");
      } else if (LIST_RE.test(line)) {
        flush();
        i = renderList(lines, i, LIST_RE.exec(line)[1].length, out);
      } else if (isTableStart(lines, i)) {
        flush();
        i = renderTable(lines, i, out);
      } else {
        paragraph.push(line);
        i++;
        while (i < lines.length && lines[i].trim() && !startsBlock(lines, i)) { paragraph.push(lines[i]); i++; }
        flush();
      }
    }
    flush();
    return out.join("");
  }

  function islandMarkdown(text) {
    return blocks(String(text).replace(/\r\n?/g, "\n").replace(/\t/g, "    ").split("\n"));
  }
  // --- Markdown (END MARKDOWN) ---
