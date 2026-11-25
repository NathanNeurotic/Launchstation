/* Launchstation Docs — self-contained search + nav. No dependencies. */
(function () {
  "use strict";
  var INDEX = [], ready = false;
  var q = document.getElementById('q');
  var box = document.getElementById('results');
  var sel = -1, current = [];
  var BASE = (document.body.getAttribute('data-base') || '');

  function load() {
    if (Array.isArray(window.WLE_DOC_INDEX)) {
      INDEX = window.WLE_DOC_INDEX;
      ready = true;
      if (q && q.value) run(q.value);
      return;
    }
    fetch(BASE + 'data/search-index.json')
      .then(function (r) { return r.json(); })
      .then(function (j) { INDEX = j; ready = true; if (q && q.value) run(q.value); })
      .catch(function () {});
  }

  function esc(s) { return s.replace(/[&<>"]/g, function (c) { return ({ '&': '&amp;', '<': '&lt;', '>': '&gt;', '"': '&quot;' })[c]; }); }

  function score(item, terms) {
    var hay = item._h || (item._h = (item.title + ' ' + (item.cat || '') + ' ' + (item.text || '')).toLowerCase());
    var s = 0;
    for (var i = 0; i < terms.length; i++) {
      var t = terms[i]; if (!t) continue;
      var ti = item.title.toLowerCase();
      if (ti === t) s += 100;
      else if (ti.indexOf(t) === 0) s += 40;
      else if (ti.indexOf(t) >= 0) s += 22;
      var n = hay.indexOf(t);
      if (n < 0) return -1;
      s += 8 + Math.max(0, 12 - n / 40);
    }
    if (item.cat === 'cheats' || item.cat === 'config' || item.cat === 'patches' || item.cat === 'igr') s += 4;
    if (item.url && item.url.indexOf('#') >= 0) s += 5;   // anchored card entries rank above full pages
    return s;
  }

  function snippet(item, terms) {
    var txt = item.text || item.desc || '';
    if (!txt) return '';
    var low = txt.toLowerCase(), at = -1;
    for (var i = 0; i < terms.length; i++) { var p = low.indexOf(terms[i]); if (p >= 0) { at = p; break; } }
    var start = at < 0 ? 0 : Math.max(0, at - 38);
    var frag = txt.slice(start, start + 150);
    if (start > 0) frag = '…' + frag;
    frag = esc(frag);
    for (var j = 0; j < terms.length; j++) {
      if (!terms[j]) continue;
      frag = frag.replace(new RegExp('(' + terms[j].replace(/[.*+?^${}()|[\]\\]/g, '\\$&') + ')', 'ig'), '<mark>$1</mark>');
    }
    return frag;
  }

  function run(val) {
    var terms = val.toLowerCase().trim().split(/\s+/).filter(Boolean);
    if (!terms.length) { box.classList.remove('open'); box.innerHTML = ''; return; }
    if (!ready) { box.innerHTML = '<div class="res-empty">loading index…</div>'; box.classList.add('open'); return; }
    var hits = [];
    for (var i = 0; i < INDEX.length; i++) { var sc = score(INDEX[i], terms); if (sc > 0) hits.push([sc, INDEX[i]]); }
    hits.sort(function (a, b) { return b[0] - a[0]; });
    current = hits.slice(0, 30).map(function (h) { return h[1]; });
    sel = -1;
    if (!current.length) { box.innerHTML = '<div class="res-empty">No matches for “' + esc(val) + '”.</div>'; box.classList.add('open'); return; }
    box.innerHTML = current.map(function (it, i) {
      return '<a class="res" data-i="' + i + '" href="' + BASE + it.url + '">' +
        '<span class="t">' + esc(it.title) + '</span><span class="c">' + esc(it.cat || '') + '</span>' +
        '<div class="s">' + snippet(it, terms) + '</div></a>';
    }).join('');
    box.classList.add('open');
  }

  function move(d) {
    var els = box.querySelectorAll('.res'); if (!els.length) return;
    sel = (sel + d + els.length) % els.length;
    for (var i = 0; i < els.length; i++) els[i].classList.toggle('sel', i === sel);
    els[sel].scrollIntoView({ block: 'nearest' });
  }

  if (q) {
    q.addEventListener('input', function () { run(q.value); });
    q.addEventListener('keydown', function (e) {
      if (e.key === 'ArrowDown') { e.preventDefault(); move(1); }
      else if (e.key === 'ArrowUp') { e.preventDefault(); move(-1); }
      else if (e.key === 'Enter') { var el = box.querySelector('.res.sel') || box.querySelector('.res'); if (el) location.href = el.href; }
      else if (e.key === 'Escape') { box.classList.remove('open'); q.blur(); }
    });
    document.addEventListener('click', function (e) { if (!e.target.closest('.search')) box.classList.remove('open'); });
    document.addEventListener('keydown', function (e) {
      if (e.key === '/' && document.activeElement !== q) { e.preventDefault(); q.focus(); }
    });
    load();
  }

  var mb = document.querySelector('.menu-btn'), sb = document.querySelector('.sidebar');
  if (mb && sb) mb.addEventListener('click', function () { sb.classList.toggle('open'); });

  document.querySelectorAll('table[data-sortable] th').forEach(function (th, ci) {
    th.style.cursor = 'pointer'; th.title = 'Sort';
    th.addEventListener('click', function () {
      var tb = th.closest('table'), rows = Array.prototype.slice.call(tb.tBodies[0].rows);
      var asc = tb.getAttribute('data-asc') !== ('' + ci); tb.setAttribute('data-asc', asc ? ci : '-' + ci);
      rows.sort(function (a, b) {
        var x = a.cells[ci].textContent.trim(), y = b.cells[ci].textContent.trim();
        var nx = parseInt(x.replace(/^[$0x]+/i, ''), 16), ny = parseInt(y.replace(/^[$0x]+/i, ''), 16);
        if (!isNaN(nx) && !isNaN(ny)) return asc ? nx - ny : ny - nx;
        return asc ? x.localeCompare(y) : y.localeCompare(x);
      });
      rows.forEach(function (r) { tb.tBodies[0].appendChild(r); });
    });
  });
})();

/* right-rail "On this page" table of contents + scroll-spy */
(function () {
  "use strict";
  var nav = document.getElementById('toc-nav');
  var aside = document.querySelector('.toc');
  if (!nav || !aside) return;
  var heads = Array.prototype.slice.call(document.querySelectorAll('.content h2, .content h3'))
    .filter(function (h) { return !h.closest('.card') && !h.closest('.step') && !h.closest('details'); });
  if (heads.length < 2) { aside.classList.add('hide'); return; }
  var used = {}, links = [], byId = {};
  heads.forEach(function (h) {
    var id = h.id;
    if (!id) {
      id = (h.textContent || '').toLowerCase().replace(/[^a-z0-9]+/g, '-').replace(/^-+|-+$/g, '').slice(0, 48) || 'sec';
      while (used[id]) id += '-x';
      h.id = id;
    }
    used[id] = 1;
    var a = document.createElement('a');
    a.href = '#' + id; a.textContent = h.textContent;
    a.className = h.tagName === 'H3' ? 'lv3' : 'lv2';
    nav.appendChild(a); links.push(a); byId[id] = a;
  });
  if ('IntersectionObserver' in window) {
    var obs = new IntersectionObserver(function (entries) {
      entries.forEach(function (e) {
        if (e.isIntersecting) {
          links.forEach(function (a) { a.classList.remove('active'); });
          if (byId[e.target.id]) byId[e.target.id].classList.add('active');
        }
      });
    }, { rootMargin: '-72px 0px -68% 0px' });
    heads.forEach(function (h) { obs.observe(h); });
  }
})();

/* project-links:start (generated, see https://github.com/NathanNeurotic/NathanNeurotic/tree/main/project-links) */
/* Small bar at the top of every page: links to the six project sites and a
   language picker that reopens THIS page through Google Translate. */
(function () {
  if (window.__nnLangbar) return;
  window.__nnLangbar = true;
  var SITES = [
    ["RiptOPL", "RiptOPL"], ["Launchstation", "Launchstation"],
    ["PS2-Servers", "PS2-Servers"], ["launcHER", "launcHER"],
    ["Simple-Media-System", "Simple-Media-System"], ["POPSLoader", "POPSLoader"]
  ];
  var LANGS = [
    ["en", "English"], ["es", "Español"], ["pt", "Português (Brasil)"],
    ["pt-PT", "Português"], ["fr", "Français"], ["de", "Deutsch"],
    ["it", "Italiano"], ["nl", "Nederlands"], ["da", "Dansk"],
    ["sv", "Svenska"], ["pl", "Polski"], ["cs", "Čeština"],
    ["hr", "Hrvatski"], ["sr", "Српски"], ["bg", "Български"],
    ["ru", "Русский"], ["el", "Ελληνικά"], ["hu", "Magyar"],
    ["ro", "Română"], ["gl", "Galego"], ["sq", "Shqip"],
    ["tr", "Türkçe"], ["ar", "العربية"], ["fa", "فارسی"],
    ["ja", "日本語"], ["ko", "한국어"], ["zh-CN", "简体中文"],
    ["zh-TW", "繁體中文"], ["vi", "Tiếng Việt"], ["lo", "ລາວ"],
    ["id", "Bahasa Indonesia"], ["tl", "Filipino"], ["ceb", "Cebuano"]
  ];
  var HOME = "https://nathanneurotic.github.io";
  var PROXY = "https://nathanneurotic-github-io.translate.goog";

  function run() {
    if (!document.body || document.getElementById("nn-langbar")) return;
    var path = location.pathname || "/";
    var proxied = /\.translate\.goog$/.test(location.hostname);
    var params = new URLSearchParams(location.search);
    var current = proxied ? (params.get("_x_tr_tl") || "en") : "en";
    ["_x_tr_sl", "_x_tr_tl", "_x_tr_hl", "_x_tr_pto", "_x_tr_hist"].forEach(function (k) { params.delete(k); });
    var rest = params.toString();
    var here = (path.split("/")[1] || "").toLowerCase();

    var bar = document.createElement("div");
    bar.id = "nn-langbar";
    bar.setAttribute("translate", "no");
    bar.className = "notranslate";
    bar.style.cssText = "font-size:12px;line-height:1.6;text-align:center;padding:4px 12px;" +
      "opacity:.85;position:relative;z-index:50;";

    SITES.forEach(function (s, i) {
      if (i) bar.appendChild(document.createTextNode(" · "));
      var el;
      if (s[1].toLowerCase() === here) {
        el = document.createElement("b");
      } else {
        el = document.createElement("a");
        el.href = HOME + "/" + s[1] + "/";
        el.style.color = "inherit";
      }
      el.textContent = s[0];
      bar.appendChild(el);
    });

    bar.appendChild(document.createTextNode("  ·  "));
    var sel = document.createElement("select");
    sel.setAttribute("aria-label", "Language");
    sel.style.cssText = "font-size:12px;background:transparent;color:inherit;border:1px solid currentColor;" +
      "border-radius:4px;padding:0 2px;";
    LANGS.forEach(function (l) {
      var o = document.createElement("option");
      o.value = l[0];
      o.textContent = l[1];
      o.style.color = "#000";
      if (l[0] === current) o.selected = true;
      sel.appendChild(o);
    });
    sel.addEventListener("change", function () {
      var code = sel.value;
      var q = rest ? rest + "&" : "";
      location.href = code === "en"
        ? HOME + path + (rest ? "?" + rest : "") + location.hash
        : PROXY + path + "?" + q + "_x_tr_sl=en&_x_tr_tl=" + code + "&_x_tr_hl=" + code + location.hash;
    });
    bar.appendChild(sel);
    var note = document.createElement("i");
    note.textContent = " automatic translation by Google";
    note.style.opacity = ".7";
    bar.appendChild(note);
    document.body.insertBefore(bar, document.body.firstChild);
  }
  if (document.readyState === "loading") document.addEventListener("DOMContentLoaded", run);
  else run();
})();
/* project-links:end */
