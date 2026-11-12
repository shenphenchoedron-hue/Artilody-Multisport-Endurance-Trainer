// Small canvas charts for the training pages (no libraries: the server is
// offline on the local network).
//   lineChart(canvas, { series: [{ name, color, points: [[x, y], ...], axis: 'left'|'right', hidden }],
//                       xFormat(x), leftLabel, rightLabel, rightMin, leftMin })
//   barChart(canvas, { labels: [...], bars: [{ name, color, values: [...] }], line: { name, color, values } })
'use strict';

const ChartsUI = {
  grid: 'rgba(185,195,240,0.18)',
  text: '#b9c3f0',
  font: '12px system-ui, sans-serif',
};

function setupCanvas(canvas) {
  const ratio = window.devicePixelRatio || 1;
  const w = canvas.clientWidth, h = canvas.clientHeight;
  canvas.width = w * ratio;
  canvas.height = h * ratio;
  const ctx = canvas.getContext('2d');
  ctx.setTransform(ratio, 0, 0, ratio, 0, 0);
  ctx.clearRect(0, 0, w, h);
  ctx.font = ChartsUI.font;
  return { ctx, w, h };
}

// a top for 4 even steps (each a 1 / 2 / 5 multiple): 250 -> 400 (100s), 0.2 -> 0.2 (0.05s)
function niceMax(v) {
  if (v <= 0) return 1;
  const step = v / 4, p = Math.pow(10, Math.floor(Math.log10(step)));
  for (const m of [1, 2, 2.5, 5, 10]) if (m * p >= step) return 4 * m * p;
  return 40 * p;
}

// a tick's text: as many decimals as its step needs
function tickText(v, step) {
  const d = step >= 1 ? 0 : Math.min(3, Math.ceil(-Math.log10(step) - 1e-9));
  return v.toFixed(d);
}

// keeps at most n points (the min and max of each bucket: peaks stay visible)
function downsample(points, n) {
  if (points.length <= n) return points;
  const out = [], size = points.length / (n / 2);
  for (let i = 0; i < points.length; i += size) {
    const chunk = points.slice(Math.floor(i), Math.floor(i + size));
    if (!chunk.length) continue;
    let lo = chunk[0], hi = chunk[0];
    for (const p of chunk) { if (p[1] < lo[1]) lo = p; if (p[1] > hi[1]) hi = p; }
    if (lo[0] < hi[0]) out.push(lo, hi); else out.push(hi, lo);
  }
  return out;
}

function lineChart(canvas, opt) {
  const { ctx, w, h } = setupCanvas(canvas);
  const L = 48, R = opt.series.some((s) => s.axis === 'right' && !s.hidden) ? 48 : 12, T = 10, B = 26;
  const shown = opt.series.filter((s) => !s.hidden && s.points.length);
  if (!shown.length) return;
  let x0 = Infinity, x1 = -Infinity;
  const ymax = { left: 0, right: 0 };
  for (const s of shown) for (const [x, y] of s.points) {
    x0 = Math.min(x0, x); x1 = Math.max(x1, x);
    if (y != null) ymax[s.axis || 'left'] = Math.max(ymax[s.axis || 'left'], y);
  }
  const ymin = { left: opt.leftMin || 0, right: opt.rightMin || 0 };
  // (from the bottom: even steps also when the axis does not start at 0)
  ymax.left = ymin.left + niceMax(ymax.left - ymin.left); ymax.right = ymin.right + niceMax(ymax.right - ymin.right);
  if (x1 <= x0) x1 = x0 + 1;
  const X = (x) => L + (x - x0) / (x1 - x0) * (w - L - R);
  const Y = (y, axis) => T + (1 - (y - ymin[axis]) / Math.max(1e-9, ymax[axis] - ymin[axis])) * (h - T - B);
  // grid and axes
  ctx.strokeStyle = ChartsUI.grid; ctx.fillStyle = ChartsUI.text; ctx.lineWidth = 1;
  for (let i = 0; i <= 4; ++i) {
    const yv = ymin.left + (ymax.left - ymin.left) * i / 4, y = Y(yv, 'left');
    ctx.beginPath(); ctx.moveTo(L, y); ctx.lineTo(w - R, y); ctx.stroke();
    ctx.textAlign = 'right'; ctx.fillText(tickText(yv, (ymax.left - ymin.left) / 4), L - 6, y + 4);
    if (R > 12) { const rv = ymin.right + (ymax.right - ymin.right) * i / 4; ctx.textAlign = 'left'; ctx.fillText(tickText(rv, (ymax.right - ymin.right) / 4), w - R + 6, Y(rv, 'right') + 4); }
  }
  ctx.textAlign = 'center';
  for (let i = 0; i <= 6; ++i) {
    const xv = x0 + (x1 - x0) * i / 6;
    ctx.fillText(opt.xFormat ? opt.xFormat(xv) : Math.round(xv), X(xv), h - 8);
  }
  for (const s of shown) {
    const axis = s.axis || 'left';
    ctx.strokeStyle = s.color; ctx.lineWidth = s.width || 1.4;
    ctx.beginPath();
    let pen = false;
    for (const [x, y] of downsample(s.points, Math.max(200, (w - L - R) * 2))) {
      if (y == null) { pen = false; continue; }
      const px = X(x), py = Y(y, axis);
      if (pen) ctx.lineTo(px, py); else ctx.moveTo(px, py);
      pen = true;
    }
    ctx.stroke();
  }
}

function barChart(canvas, opt) {
  const { ctx, w, h } = setupCanvas(canvas);
  const L = 44, R = opt.line ? 44 : 10, T = 10, B = 26, n = opt.labels.length;
  if (!n) return;
  const bmax = niceMax(Math.max(0, ...opt.bars.flatMap((b) => b.values)));
  const lmax = opt.line ? niceMax(Math.max(0, ...opt.line.values)) : 1;
  const slot = (w - L - R) / n, bw = slot * 0.7 / opt.bars.length;
  const Y = (v, m) => T + (1 - v / m) * (h - T - B);
  ctx.strokeStyle = ChartsUI.grid; ctx.fillStyle = ChartsUI.text; ctx.lineWidth = 1;
  for (let i = 0; i <= 4; ++i) {
    const y = Y(bmax * i / 4, bmax);
    ctx.beginPath(); ctx.moveTo(L, y); ctx.lineTo(w - R, y); ctx.stroke();
    ctx.textAlign = 'right'; ctx.fillText(tickText(bmax * i / 4, bmax / 4), L - 6, y + 4);
    if (opt.line) { ctx.textAlign = 'left'; ctx.fillText(tickText(lmax * i / 4, lmax / 4), w - R + 6, Y(lmax * i / 4, lmax) + 4); }
  }
  opt.bars.forEach((b, bi) => {
    ctx.fillStyle = b.color;
    b.values.forEach((v, i) => {
      const x = L + i * slot + slot * 0.15 + bi * bw;
      ctx.fillRect(x, Y(v, bmax), bw - 1, Y(0, bmax) - Y(v, bmax));
    });
  });
  ctx.fillStyle = ChartsUI.text; ctx.textAlign = 'center';
  const every = Math.ceil(n / 12);
  opt.labels.forEach((l, i) => { if (i % every === 0) ctx.fillText(l, L + i * slot + slot / 2, h - 8); });
  if (opt.line) {
    ctx.strokeStyle = opt.line.color; ctx.lineWidth = 2; ctx.beginPath();
    opt.line.values.forEach((v, i) => { const x = L + i * slot + slot / 2, y = Y(v, lmax); if (i) ctx.lineTo(x, y); else ctx.moveTo(x, y); });
    ctx.stroke();
    ctx.fillStyle = opt.line.color;
    opt.line.values.forEach((v, i) => { ctx.beginPath(); ctx.arc(L + i * slot + slot / 2, Y(v, lmax), 2.5, 0, 7); ctx.fill(); });
  }
}

function legend(el, items, onToggle) {
  el.innerHTML = '';
  items.forEach((s, i) => {
    const b = document.createElement('label');
    b.style.cssText = 'display:inline-flex;align-items:center;gap:6px;margin-right:16px;cursor:pointer;color:#d6dcfa';
    b.innerHTML = '<input type="checkbox" ' + (s.hidden ? '' : 'checked') + '><span style="display:inline-block;width:14px;height:3px;background:' + s.color + '"></span>' + s.name;
    b.querySelector('input').addEventListener('change', (e) => onToggle(i, !e.target.checked));
    el.appendChild(b);
  });
}
