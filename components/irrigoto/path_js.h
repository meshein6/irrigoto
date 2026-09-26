/* Auto-generated from path.js -- edit the .html, then run regen.py */
#ifdef IRRIGOTO_HTML_PAYLOAD
R"PATHJS(
/* irrigoto shared path geometry -- served as /path.js (b535).
 *
 * Builds the ring/arc geometry for a zone and lays a watering PATH over it,
 * so the Water modal and each schedule entry can show what a mode will
 * actually do before it runs.
 *
 * This file is the single copy: zone_setup.html, landing.html and
 * schedule.html all load it. The ring construction and polygon clipping
 * mirror phase_water_zone() in irrigoto.c -- zone max throw down to zone
 * min, stepping by max(700 * t/act_max, 80) mm, plus inner rings when the
 * sprinkler stands inside the polygon.
 *
 * It is an APPROXIMATION of the firmware's plan, not a readback. Smooth's
 * real ring order is chosen at run time from per-ring deficit and supply
 * trend, so it is drawn outer-to-inner and labelled as varying. For an
 * exact picture the firmware would have to expose its own plan.
 */
(function (root) {
  'use strict';

  var RING_MAX = 36;
  var STEP_DEG = 0.5;           /* polygon-clip resolution, as drawPath used */
  var MIN_THROW = 461;          /* WATER_MIN_THROW_MM */

  /* Modes, by the web digit the UI already uses. */
  var MODES = {
    '1': { key: 'pulse',      label: 'Pulse' },
    '5': { key: 'gentle',     label: 'Gentle' },
    '7': { key: 'smooth',     label: 'Smooth' },
    '8': { key: 'serpentine', label: 'Serpentine' },
    '9': { key: 'sections',   label: 'Sections' },
    'c': { key: 'chase',      label: 'Chase' },
    'd': { key: 'demo',       label: 'Demo' }
  };

  function rad(d) { return (d - 90) * Math.PI / 180; }

  /* The two endpoints that carry zone geometry disagree on the field name:
   * /zone/state (Zone Setup) sends {deg, throw_mm}, /api/all (landing and
   * schedule) sends {deg, mm}. Normalising here means callers can hand over
   * whatever they were given -- the mismatch used to draw an empty circle. */
  function normPoints(pts) {
    if (!pts || !pts.length) return [];
    var out = [], i, p, t;
    for (i = 0; i < pts.length; i++) {
      p = pts[i];
      if (!p) continue;
      t = (p.throw_mm !== undefined) ? p.throw_mm
        : (p.mm !== undefined)       ? p.mm
        : (p.r !== undefined)        ? p.r : undefined;
      if (t === undefined || !(t > 0)) continue;
      out.push({ deg: +p.deg || 0, throw_mm: +t });
    }
    return out;
  }

  /* Even-odd point-in-polygon in (bearing, throw) space, translated so the
   * test point is the origin -- same method the page used before. */
  function pointInZone(pts, bearing, r_mm) {
    var px = r_mm * Math.sin(bearing * Math.PI / 180),
        py = r_mm * Math.cos(bearing * Math.PI / 180),
        crosses = 0, n = pts.length;
    for (var i = 0; i < n; i++) {
      var j = (i + 1) % n, pi = pts[i], pj = pts[j];
      var x1 = pi.throw_mm * Math.sin(pi.deg * Math.PI / 180) - px,
          y1 = pi.throw_mm * Math.cos(pi.deg * Math.PI / 180) - py,
          x2 = pj.throw_mm * Math.sin(pj.deg * Math.PI / 180) - px,
          y2 = pj.throw_mm * Math.cos(pj.deg * Math.PI / 180) - py;
      if ((y1 > 0) !== (y2 > 0)) {
        var t = y1 / (y1 - y2);
        if (x1 + t * (x2 - x1) > 0) crosses++;
      }
    }
    return (crosses % 2) === 1;
  }

  /* The zone's active arc: the largest gap between vertex bearings is treated
   * as the excluded sector. Three cases force a full 360: all points at one
   * throw (sprinkler centred), the origin inside the polygon, and a long
   * straight edge that merely spans the gap (b435). */
  function zoneArc(pts) {
    var sdegs = pts.map(function (p) { return p.deg; }).sort(function (a, b) { return a - b; });
    var mg = 0, gi = 0;
    for (var i = 0; i < sdegs.length; i++) {
      var nx = i < sdegs.length - 1 ? sdegs[i + 1] : sdegs[0] + 360;
      if (nx - sdegs[i] > mg) { mg = nx - sdegs[i]; gi = i; }
    }
    var start = sdegs[(gi + 1) % sdegs.length], end = sdegs[gi];
    var span = ((end - start + 360) % 360) || 360;

    var throws = pts.map(function (p) { return p.throw_mm; });
    var zmax = Math.max.apply(null, throws), zmin2 = Math.min.apply(null, throws);
    var centred = zmax > 0 && (zmax - zmin2) / zmax < 0.05;
    if (centred) return { start: 0, span: 360, centred: true, originInside: false, gap: mg, gi: gi };

    var inside = pointInZone(pts, 0, 1);
    if (inside) return { start: 0, span: 360, centred: false, originInside: true, gap: mg, gi: gi };

    if (span < 360 && mg > 0) {
      for (var k = 1; k <= 3; k++) {
        var gb = (sdegs[gi] + mg * k / 4) % 360;
        var gbs = Math.sin(gb * Math.PI / 180), gbc = Math.cos(gb * Math.PI / 180), best = 0;
        for (var a = 0; a < pts.length; a++) {
          var b = (a + 1) % pts.length, pa = pts[a], pb = pts[b];
          var ax = pa.throw_mm * Math.sin(pa.deg * Math.PI / 180),
              ay = pa.throw_mm * Math.cos(pa.deg * Math.PI / 180),
              bx = pb.throw_mm * Math.sin(pb.deg * Math.PI / 180),
              by = pb.throw_mm * Math.cos(pb.deg * Math.PI / 180);
          var dx = bx - ax, dy = by - ay, det = gbc * dx - gbs * dy;
          if (Math.abs(det) < 1e-6) continue;
          var s = (dx * ay - dy * ax) / det, u = (gbs * ay - gbc * ax) / det;
          if (s > 0.5 && u >= 0 && u <= 1 && s > best) best = s;
        }
        if (best >= MIN_THROW) return { start: 0, span: 360, centred: false, originInside: false, gap: mg, gi: gi };
      }
    }
    return { start: start, span: span, centred: false, originInside: false, gap: mg, gi: gi };
  }

  /* Ring radii, outer to inner. */
  function ringThrows(pts, actMax, actMin, arc) {
    var throws = pts.map(function (p) { return p.throw_mm; });
    var zmax = Math.max.apply(null, throws);
    var zmin = (actMin && actMin > 50) ? actMin : Math.min.apply(null, throws);
    var rings = [], t = zmax;
    while (t >= zmin && rings.length < RING_MAX) {
      rings.push(t);
      t -= Math.max(700 * (t / actMax), 80);
    }
    if (!arc.centred && arc.originInside && zmin > MIN_THROW) {
      while (t > MIN_THROW && rings.length < RING_MAX) {
        rings.push(t);
        t -= Math.max(700 * (t / actMax), 80);
      }
    }
    return rings;
  }

  /* The wetted spans of one ring: walk the arc and keep what's inside. */
  function ringSpans(pts, arc, thr) {
    var spans = [], spanStart = null;
    for (var o = 0; o <= arc.span + STEP_DEG; o += STEP_DEG) {
      var bearing = (arc.start + o) % 360, inside = pointInZone(pts, bearing, thr);
      if (inside && spanStart === null) spanStart = o;
      if (!inside && spanStart !== null) {
        spans.push({ lo: (arc.start + spanStart + 360) % 360, span: o - spanStart });
        spanStart = null;
      }
    }
    if (spanStart !== null)
      spans.push({ lo: (arc.start + spanStart + 360) % 360, span: arc.span - spanStart });
    return spans.filter(function (s) { return s.span >= 0.5; });
  }

  /* Visit order and sweep direction per mode.
   *
   * This corrects a mismatch in the old drawPath, which alternated direction
   * every ring (cw = ri % 2 === 0) -- true only for Serpentine.
   *   pulse       outer -> inner, always CW, dry swing back between rings
   *   gentle      outer -> inner, one direction per pass, flipped each pass
   *   smooth      order varies at run time; drawn outer -> inner
   *   serpentine  pass 1 out -> in, pass 2 in -> out; flips every ring, wet turns
   * A zone set to sequential ring order (b535) alternates every ring for all
   * modes and never swings back.
   */
  function planOrder(modeKey, nRings, pass, sequential) {
    var idx = [], i;
    var serpish = (modeKey === 'serpentine' || modeKey === 'sections');
    var inward = !(serpish && (pass % 2) === 1);
    for (i = 0; i < nRings; i++) idx.push(inward ? i : nRings - 1 - i);

    var cw = [], dryReturn = [];
    for (i = 0; i < nRings; i++) {
      var d;
      if (sequential)                    d = (i % 2) === 0;
      else if (serpish)                  d = (i % 2) === 0;
      else if (modeKey === 'pulse')      d = true;
      else                               d = (pass % 2) === 0;  /* gentle, smooth */
      cw.push(d);
      /* A ring that sweeps the same direction as the last one has to travel
       * back to its start with the valve shut. */
      dryReturn.push(i > 0 && cw[i] === cw[i - 1]);
    }
    return { idx: idx, cw: cw, dryReturn: dryReturn, orderVaries: (modeKey === 'smooth' && !sequential) };
  }

  /* Do two circular bearing ranges overlap? Mirrors serp_arc_overlaps() in
   * irrigoto.c so the preview groups arcs into lobes the same way the
   * firmware's plan builder does. */
  function spanOverlaps(a, b) {
    var step = 2.0;
    for (var t = 0; t <= a.span; t += step) {
      var off = (((a.lo + t) % 360) - b.lo + 360) % 360;
      if (off <= b.span) return true;
    }
    return false;
  }

  /* Lobe-major ordering for Sections: finish one side of the zone outer ->
   * inner, then ONE hop to the next, instead of crossing on every ring.
   * Lobes are the outermost waterable ring's arcs; going inward they merge,
   * so an arc belongs to the lowest-index lobe it overlaps and merged rings
   * are swept exactly once, under the first section. Mirrors the firmware. */
  function orderSections(ringsIn) {
    /* b541: lobes come from the ring with the MOST arcs, not the outermost --
     * a zone with a waist is one arc at the outer rings and splits inward, so
     * reading the outermost ring found a single lobe and disabled sections. */
    var lobes = null, i, j;
    for (i = 0; i < ringsIn.length; i++) {
      if (!lobes || ringsIn[i].spans.length > lobes.length) lobes = ringsIn[i].spans;
    }
    if (!lobes || lobes.length < 2) return null;   /* nothing to section */
    var out = [], visit = 0;
    for (var L = 0; L < lobes.length; L++) {
      for (i = 0; i < ringsIn.length; i++) {
        var R = ringsIn[i], sub = [];
        for (j = 0; j < R.spans.length; j++) {
          var owner = -1;
          for (var k = 0; k < lobes.length; k++)
            if (spanOverlaps(R.spans[j], lobes[k])) { owner = k; break; }
          if (owner < 0) owner = 0;
          if (owner === L) sub.push(R.spans[j]);
        }
        if (!sub.length) continue;
        out.push({ ring: R.ring, visit: visit++, throw_mm: R.throw_mm,
                   cw: (out.length % 2) === 0, lobe: L, spans: sub });
      }
    }
    /* A hop is dry whenever the next visit is a different ring or lobe. */
    for (i = 0; i < out.length; i++)
      out[i].dryReturn = (i > 0) && (out[i].lobe !== out[i - 1].lobe ||
                                     out[i].cw === out[i - 1].cw);
    return out.length ? out : null;
  }

  /* Build everything needed to draw. `points` is [{deg, throw_mm}, ...]. */
  function build(points, opts) {
    opts = opts || {};
    points = normPoints(points);
    if (points.length < 2) return null;
    var actMax = opts.act_max_throw || 10058;
    var actMin = opts.act_min_throw || 0;
    var modeKey = (MODES[opts.mode] || MODES['1']).key;
    if (modeKey === 'chase' || modeKey === 'demo') return { modeKey: modeKey, rings: [], noPath: true };

    var arc = zoneArc(points);
    var thr = ringThrows(points, actMax, actMin, arc);
    if (!thr.length) return null;

    var plan = planOrder(modeKey, thr.length, opts.pass || 0, !!opts.sequential);
    var rings = [];
    for (var v = 0; v < plan.idx.length; v++) {
      var ri = plan.idx[v];
      rings.push({
        ring: ri,
        visit: v,
        throw_mm: thr[ri],
        cw: plan.cw[v],
        dryReturn: plan.dryReturn[v],
        spans: ringSpans(points, arc, thr[ri])
      });
    }
    /* b540: Sections reorders the visits lobe-major. Without this the preview
     * drew Sections and Serpentine identically -- it claimed a behaviour the
     * firmware has but the preview never modelled. */
    var sectioned = (modeKey === 'sections') ? orderSections(rings) : null;
    if (sectioned) rings = sectioned;

    return {
      modeKey: modeKey,
      modeLabel: (MODES[opts.mode] || MODES['1']).label,
      arc: arc,
      lobes: sectioned ? (sectioned[sectioned.length - 1].lobe + 1) : 1,
      rings: rings,
      scale_mm: opts.scale_mm || (actMax + 914),
      orderVaries: plan.orderVaries,
      noPath: false
    };
  }

  /* Draw into ctx. cx/cy/maxR define the radar circle; scale_mm is the mm at
   * the rim (pass the page's own zoom scale to stay in step with it). */
  function draw(ctx, geom, o) {
    if (!geom || geom.noPath || !geom.rings.length) return;
    o = o || {};
    var cx = o.cx, cy = o.cy, maxR = o.maxR;
    var scale = o.scale_mm || geom.scale_mm;
    var n = geom.rings.length;
    var thin = !!o.thumb;

    for (var i = 0; i < n; i++) {
      var R = geom.rings[i];
      var r = (R.throw_mm / scale) * maxR;
      if (!(r > 0) || r > maxR * 1.02) continue;
      /* Fade with visit order so the sequence reads without a legend. */
      var al = 0.75 - 0.45 * (i / (n - 1 || 1));
      var col = R.cw ? 'rgba(80,180,255,' + al + ')' : 'rgba(255,160,60,' + al + ')';
      for (var s = 0; s < R.spans.length; s++) {
        var sp = R.spans[s], sa = rad(sp.lo);
        ctx.beginPath();
        ctx.arc(cx, cy, r, sa, sa + sp.span * Math.PI / 180, false);
        ctx.strokeStyle = col;
        ctx.lineWidth = thin ? 1.2 : 1.8;
        ctx.setLineDash([]);
        ctx.stroke();
        if (!thin && sp.span > 8) arrow(ctx, cx, cy, r, sp, R.cw);
      }
      /* Dry return to the next ring's start, when the direction repeats. */
      if (!thin && i + 1 < n && geom.rings[i + 1].dryReturn && R.spans.length) {
        var nx = geom.rings[i + 1];
        var nr = (nx.throw_mm / scale) * maxR;
        if (nx.spans.length) {
          var from = R.cw ? R.spans[R.spans.length - 1] : R.spans[0];
          var fb = R.cw ? (from.lo + from.span) : from.lo;
          var to = nx.cw ? nx.spans[0] : nx.spans[nx.spans.length - 1];
          var tb = nx.cw ? to.lo : (to.lo + to.span);
          ctx.beginPath();
          ctx.moveTo(cx + Math.cos(rad(fb)) * r, cy + Math.sin(rad(fb)) * r);
          ctx.lineTo(cx + Math.cos(rad(tb)) * nr, cy + Math.sin(rad(tb)) * nr);
          ctx.strokeStyle = 'rgba(150,150,150,.35)';
          ctx.lineWidth = 1;
          ctx.setLineDash([2, 4]);
          ctx.stroke();
          ctx.setLineDash([]);
        }
      }
    }
  }

  /* Flatten the plan into the ordered list of moves the nozzle makes, so a
   * scrubber can walk it. Wet segments are ring arcs; dry segments are the
   * valve-closed hops between them. Lengths are real distances in mm, so the
   * dot moves at a believable speed rather than jumping ring to ring. */
  function flatten(geom) {
    if (!geom || geom.noPath || !geom.rings.length) return { segs: [], total: 0 };
    var segs = [], prev = null;
    for (var i = 0; i < geom.rings.length; i++) {
      var R = geom.rings[i];
      /* Sweep each span in travel order. */
      var order = R.spans.slice().sort(function (a, b) {
        return R.cw ? (a.lo - b.lo) : (b.lo - a.lo);
      });
      for (var j = 0; j < order.length; j++) {
        var sp = order[j];
        var from = R.cw ? sp.lo : (sp.lo + sp.span);
        var to   = R.cw ? (sp.lo + sp.span) : sp.lo;
        if (prev) {
          var dl = polarDist(prev.r, prev.deg, R.throw_mm, from);
          if (dl > 1) segs.push({ dry: true, r0: prev.r, d0: prev.deg,
                                  r1: R.throw_mm, d1: from, len: dl });
        }
        var len = R.throw_mm * (sp.span * Math.PI / 180);
        segs.push({ dry: false, ring: R.ring, visit: i, r: R.throw_mm,
                    from: from, to: to, cw: R.cw, len: Math.max(len, 1) });
        prev = { r: R.throw_mm, deg: to };
      }
    }
    var total = segs.reduce(function (a, sg) { return a + sg.len; }, 0);
    return { segs: segs, total: total };
  }

  function polarDist(r0, d0, r1, d1) {
    var a0 = rad(d0), a1 = rad(d1);
    var x0 = Math.cos(a0) * r0, y0 = Math.sin(a0) * r0;
    var x1 = Math.cos(a1) * r1, y1 = Math.sin(a1) * r1;
    return Math.hypot(x1 - x0, y1 - y0);
  }

  /* Position at fraction t (0..1) along the flattened path. */
  function pointAt(flat, t) {
    if (!flat || !flat.segs.length) return null;
    var want = Math.max(0, Math.min(1, t)) * flat.total, acc = 0;
    for (var i = 0; i < flat.segs.length; i++) {
      var sg = flat.segs[i];
      if (acc + sg.len >= want || i === flat.segs.length - 1) {
        var f = sg.len > 0 ? (want - acc) / sg.len : 0;
        f = Math.max(0, Math.min(1, f));
        if (sg.dry) {
          return { r_mm: sg.r0 + (sg.r1 - sg.r0) * f,
                   bearing: sg.d0 + angDelta(sg.d0, sg.d1) * f,
                   dry: true, ring: -1 };
        }
        return { r_mm: sg.r, bearing: sg.from + (sg.to - sg.from) * f,
                 dry: false, ring: sg.ring, visit: sg.visit, cw: sg.cw };
      }
      acc += sg.len;
    }
    return null;
  }

  function angDelta(a, b) { var d = (b - a) % 360; if (d > 180) d -= 360; if (d < -180) d += 360; return d; }

  /* Draw the scrub marker: a dot at `t`, hollow while the valve is shut. */
  function marker(ctx, flat, t, o) {
    var pt = pointAt(flat, t);
    if (!pt) return null;
    var r = (pt.r_mm / o.scale_mm) * o.maxR;
    var a = rad(pt.bearing);
    var x = o.cx + Math.cos(a) * r, y = o.cy + Math.sin(a) * r;
    /* Radial line from the sprinkler -- this is where the stream points. */
    ctx.beginPath();
    ctx.moveTo(o.cx, o.cy); ctx.lineTo(x, y);
    ctx.strokeStyle = pt.dry ? 'rgba(160,160,160,.35)' : 'rgba(0,232,122,.45)';
    ctx.lineWidth = 1.2; ctx.setLineDash(pt.dry ? [3, 4] : []);
    ctx.stroke(); ctx.setLineDash([]);
    ctx.beginPath(); ctx.arc(x, y, 6, 0, Math.PI * 2);
    if (pt.dry) {
      ctx.strokeStyle = 'rgba(200,200,200,.9)'; ctx.lineWidth = 2; ctx.stroke();
    } else {
      ctx.fillStyle = '#00e87a'; ctx.shadowBlur = 14; ctx.shadowColor = '#00e87a';
      ctx.fill(); ctx.shadowBlur = 0;
    }
    return pt;
  }

  function arrow(ctx, cx, cy, r, sp, cw) {
    var mid = sp.lo + sp.span * (cw ? 0.62 : 0.38);
    var a = rad(mid);
    var x = cx + Math.cos(a) * r, y = cy + Math.sin(a) * r;
    /* Tangent, pointing the way the nozzle travels. */
    var t = a + (cw ? Math.PI / 2 : -Math.PI / 2);
    var L = 5;
    ctx.beginPath();
    ctx.moveTo(x + Math.cos(t) * L, y + Math.sin(t) * L);
    ctx.lineTo(x + Math.cos(t + 2.5) * L, y + Math.sin(t + 2.5) * L);
    ctx.lineTo(x + Math.cos(t - 2.5) * L, y + Math.sin(t - 2.5) * L);
    ctx.closePath();
    ctx.fillStyle = ctx.strokeStyle;
    ctx.fill();
  }

  /* Draw a whole thumbnail: background disc, zone outline, then the path.
   * Used by the Water modal and the schedule entry cards. */
  function thumb(canvas, points, opts) {
    if (!canvas || !canvas.getContext) return false;
    var ctx = canvas.getContext('2d');
    var W = canvas.width, H = canvas.height;
    ctx.clearRect(0, 0, W, H);
    var cx = W / 2, cy = H / 2, maxR = Math.min(W, H) / 2 - 2;

    var css = getComputedStyle(document.documentElement);
    var bg = (css.getPropertyValue('--radar-bg') || '#0a1a12').trim();
    ctx.fillStyle = bg;
    ctx.beginPath(); ctx.arc(cx, cy, maxR, 0, Math.PI * 2); ctx.fill();

    points = normPoints(points);
    if (points.length < 2) return false;
    var geom = build(points, opts);
    if (!geom) return false;

    /* Fit the thumbnail to the zone; full reach wastes most of the disc. */
    var zmax = Math.max.apply(null, points.map(function (p) { return p.throw_mm; }));
    var scale = Math.max(zmax * 1.12, 600);

    ctx.strokeStyle = 'rgba(120,140,130,.45)';
    ctx.lineWidth = 1;
    ctx.beginPath();
    points.forEach(function (p, i) {
      var rr = (p.throw_mm / scale) * maxR, a = rad(p.deg);
      var x = cx + Math.cos(a) * rr, y = cy + Math.sin(a) * rr;
      i ? ctx.lineTo(x, y) : ctx.moveTo(x, y);
    });
    ctx.closePath(); ctx.stroke();

    if (geom.noPath) {
      ctx.fillStyle = 'rgba(150,160,155,.8)';
      ctx.font = '10px -apple-system,sans-serif';
      ctx.textAlign = 'center'; ctx.textBaseline = 'middle';
      ctx.fillText(geom.modeKey === 'chase' ? 'no path' : 'demo', cx, cy);
      return true;
    }
    var dopt = { cx: cx, cy: cy, maxR: maxR, scale_mm: scale, thumb: !!opts.thumb };
    draw(ctx, geom, dopt);
    /* Optional scrub marker (opts.scrub is 0..1). */
    var at = null;
    if (typeof opts.scrub === 'number') at = marker(ctx, flatten(geom), opts.scrub, dopt);
    /* Sprinkler at the centre. */
    ctx.fillStyle = 'rgba(0,232,122,.9)';
    ctx.beginPath(); ctx.arc(cx, cy, 2, 0, Math.PI * 2); ctx.fill();
    return at || true;
  }

  /* ── Shared full-screen preview ────────────────────────────────────────
   * One overlay, used by Zone Setup, the Water modal and each schedule entry,
   * so the picture is identical wherever it is opened. `lockMode` fixes the
   * mode to what the caller already chose (manual run / schedule entry) and
   * hides the mode chips; Zone Setup leaves it unlocked so a zone can be
   * compared across modes.
   *
   * open({points, act_max_throw, act_min_throw, mode, lockMode, title}) */
  var OV_MODES = ['1', '5', '7', '8', '9'];
  var ov = null;

  function ovBuild() {
    if (ov) return ov;
    var el = document.createElement('div');
    el.id = 'irr-path-ov';
    el.innerHTML =
      '<div class="ipo-bar">' +
        '<span class="ipo-title"></span>' +
        '<span class="ipo-modes"></span>' +
        '<button class="ipo-btn ipo-pass">Pass 1</button>' +
        '<button class="ipo-btn ipo-x" aria-label="Close">&#10005;</button>' +
      '</div>' +
      '<canvas class="ipo-cv" width="620" height="620"></canvas>' +
      '<div class="ipo-scrub">' +
        '<button class="ipo-btn ipo-play" aria-label="Play">&#9654;</button>' +
        '<input type="range" min="0" max="1000" step="1" value="0" aria-label="Position along the path">' +
        '<span class="ipo-at">start</span>' +
      '</div>' +
      '<div class="ipo-note"></div>';
    var css = document.createElement('style');
    css.textContent =
      '#irr-path-ov{display:none;position:fixed;inset:0;background:rgba(0,0,0,.88);' +
        'z-index:300;align-items:center;justify-content:center;flex-direction:column;gap:12px;}' +
      '#irr-path-ov.open{display:flex;}' +
      '#irr-path-ov .ipo-cv{max-width:92vw;max-height:66vh;border-radius:50%;}' +
      '#irr-path-ov .ipo-bar,#irr-path-ov .ipo-scrub{display:flex;gap:8px;align-items:center;' +
        'width:min(92vw,620px);color:var(--text-mid);font-size:12px;}' +
      '#irr-path-ov .ipo-title{flex:1;color:var(--text);}' +
      '#irr-path-ov .ipo-modes{display:flex;gap:4px;}' +
      '#irr-path-ov .ipo-btn{background:var(--btn);border:1px solid var(--border);' +
        'color:var(--text);font-size:11px;padding:6px 10px;border-radius:6px;' +
        'cursor:pointer;font-family:inherit;}' +
      '#irr-path-ov .ipo-btn.sel{border-color:var(--green);background:var(--green-dim);color:var(--green);}' +
      '#irr-path-ov .ipo-scrub input{flex:1;min-width:0;}' +
      '#irr-path-ov .ipo-at{font-family:"Courier New",monospace;font-size:11px;' +
        'min-width:96px;text-align:right;}' +
      '#irr-path-ov .ipo-note{width:min(92vw,620px);font-size:11px;' +
        'color:var(--text-mid);text-align:center;min-height:14px;}';
    document.head.appendChild(css);
    document.body.appendChild(el);
    ov = {
      el: el,
      cv: el.querySelector('.ipo-cv'),
      title: el.querySelector('.ipo-title'),
      modes: el.querySelector('.ipo-modes'),
      pass: el.querySelector('.ipo-pass'),
      play: el.querySelector('.ipo-play'),
      range: el.querySelector('input'),
      at: el.querySelector('.ipo-at'),
      note: el.querySelector('.ipo-note'),
      opts: null, mode: '7', passIdx: 0, t: 0, timer: null,
      variants: [], passNoteText: ''
    };
    el.addEventListener('click', function (e) { if (e.target === el) ovClose(); });
    ov.el.querySelector('.ipo-x').addEventListener('click', ovClose);
    ov.pass.addEventListener('click', function () {
      ov.passIdx = (ov.passIdx + 1) % Math.max(1, ov.variants.length);
      ovPassLabel();
      ovDraw();
    });
    ov.range.addEventListener('input', function () { ov.t = +ov.range.value / 1000; ovDraw(); });
    ov.play.addEventListener('click', ovPlay);
    document.addEventListener('keydown', function (e) { if (e.key === 'Escape') ovClose(); });
    return ov;
  }

  function ovPlay() {
    if (ov.timer) { clearInterval(ov.timer); ov.timer = null; ov.play.innerHTML = '&#9654;'; return; }
    ov.play.innerHTML = '&#9632;';
    ov.timer = setInterval(function () {      /* ~8 s a pass, slow enough to follow */
      ov.t += 1 / 240;
      if (ov.t >= 1) { ov.t = 1; clearInterval(ov.timer); ov.timer = null; ov.play.innerHTML = '&#9654;'; }
      ov.range.value = Math.round(ov.t * 1000);
      ovDraw();
    }, 33);
  }

  /* b544: show the DISTINCT pictures, not a pass count.
   *
   * b542 put the real caps on the button and they read as nonsense: "Pass 1
   * of up to 30". Thirty is a safety ceiling on an adaptive loop that exits
   * as soon as every ring meets target -- it is not a plan, and quoting it
   * suggests the sprinkler intends thirty laps.
   *
   * It was also claiming variety that does not exist. Only two things change
   * between passes: sweep direction, and for serpentine/sections whether the
   * rings run outward or inward. Both flip every pass, so pass 3 is pass 1
   * again. There are at most TWO different pictures for any mode, and Pulse
   * repeats one picture depth8 times.
   *
   * So the control steps between those, labelled by what actually differs,
   * and the repeat count is stated in words instead of as a fake total. */
  function passVariants(modeKey) {
    if (modeKey === 'chase' || modeKey === 'demo') return [];
    if (modeKey === 'pulse') return [];               /* every pass identical */
    if (modeKey === 'serpentine' || modeKey === 'sections')
      return ['Outer \u2192 in', 'Inner \u2192 out'];
    return ['Sweep one way', 'Sweep back'];           /* gentle, smooth */
  }

  /* What the run actually does with those, in plain words. */
  function passNote(modeKey, depth8) {
    var d = (depth8 >= 1 && depth8 <= 8) ? depth8 : 1;
    if (modeKey === 'chase' || modeKey === 'demo') return '';
    if (modeKey === 'pulse')
      return d > 1 ? 'Repeats this pass ' + d + ' times' : 'One pass';
    if (modeKey === 'smooth')
      return 'Alternates until every ring reaches the target depth; the ring '
           + 'order is chosen during the run';
    return 'Alternates until every ring reaches the target depth';
  }

  function ovPassLabel() {
    var v = ov.variants;
    ov.pass.textContent = v.length ? v[ov.passIdx % v.length] : '';
    if (ov.note) ov.note.textContent = ov.passNoteText || '';
  }

  function ovClose() {
    if (!ov) return;
    if (ov.timer) { clearInterval(ov.timer); ov.timer = null; ov.play.innerHTML = '&#9654;'; }
    ov.el.classList.remove('open');
  }

  function ovDraw() {
    var o = {
      mode: ov.mode, pass: ov.passIdx, scrub: ov.t,
      act_max_throw: ov.opts.act_max_throw, act_min_throw: ov.opts.act_min_throw
    };
    var at = thumb(ov.cv, ov.opts.points, o);
    var label = (MODES[ov.mode] || {}).label || '';
    ov.title.textContent = (ov.opts.title ? ov.opts.title + ' \u00b7 ' : '') + label;
    ov.at.textContent = (at && at.r_mm !== undefined)
      ? (at.dry ? 'moving \u00b7 dry'
                : 'ring ' + (at.ring + 1) + ' \u00b7 ' + (at.r_mm / 304.8).toFixed(1) + "'")
      : (ov.t <= 0 ? 'start' : '');
  }

  function openPreview(opts) {
    if (!opts) return false;
    var pts = normPoints(opts.points);
    if (pts.length < 2) return false;
    ovBuild();
    ov.opts = opts;
    ov.opts.points = pts;
    ov.mode = MODES[opts.mode] ? opts.mode : '7';
    ov.passIdx = 0; ov.t = 0; ov.range.value = 0;
    var mk = (MODES[ov.mode] || MODES['1']).key;
    ov.variants = passVariants(mk);
    ov.passNoteText = passNote(mk, opts.depth8);
    /* Nothing to step through when every pass looks the same. */
    ov.pass.style.display = ov.variants.length ? '' : 'none';
    ovPassLabel();
    /* Locked: the caller already chose the mode, so show it as a static chip
     * rather than letting the preview disagree with the run that will happen. */
    ov.modes.innerHTML = '';
    if (opts.lockMode) {
      var tag = document.createElement('span');
      tag.className = 'ipo-btn sel';
      tag.style.cursor = 'default';
      tag.textContent = (MODES[ov.mode] || {}).label || '';
      ov.modes.appendChild(tag);
    } else {
      OV_MODES.forEach(function (m) {
        var b = document.createElement('button');
        b.className = 'ipo-btn' + (m === ov.mode ? ' sel' : '');
        b.textContent = (MODES[m] || {}).label || m;
        b.addEventListener('click', function () {
          ov.mode = m;
          var k = (MODES[m] || MODES['1']).key;
          ov.variants = passVariants(k);
          ov.passNoteText = passNote(k, ov.opts.depth8);
          if (ov.passIdx >= ov.variants.length) ov.passIdx = 0;
          ov.pass.style.display = ov.variants.length ? '' : 'none';
          ovPassLabel();
          ov.modes.querySelectorAll('.ipo-btn').forEach(function (x) { x.classList.toggle('sel', x === b); });
          ovDraw();
        });
        ov.modes.appendChild(b);
      });
    }
    ov.el.classList.add('open');
    ovDraw();
    return true;
  }

  root.IrrigotoPath = {
    openPreview: openPreview, closePreview: ovClose,
    MODES: MODES, build: build, draw: draw, thumb: thumb, normPoints: normPoints,
    flatten: flatten, pointAt: pointAt, marker: marker,
    zoneArc: zoneArc, ringThrows: ringThrows, ringSpans: ringSpans,
    pointInZone: pointInZone
  };
})(window);
)PATHJS"
#endif /* IRRIGOTO_HTML_PAYLOAD */
