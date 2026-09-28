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

  /* b594: which modes actually follow a plan.
   *
   * Serpentine and Sections are scheduled up front -- every ring's pass count
   * is solved before the first drop, and the run executes exactly that
   * (water_serpentine_passes, b575). For those, a scrubber is meaningful:
   * dragging it shows the run that WILL happen.
   *
   * Pulse, Gentle and Smooth are adaptive. Their ring order and pass count are
   * decided during the run from measured deficit and supply trend, so any
   * animation of them is a guess dressed up as a schedule. They get the path
   * and a sentence saying how they use it, and no transport controls -- the
   * honest answer to "when will it be here" is that nobody knows yet. */
  function isDeterministic(modeKey) {
    return modeKey === 'serpentine' || modeKey === 'sections';
  }

  /* What an adaptive mode does with the rings, in one sentence. */
  var ADAPTIVE_NOTE = {
    pulse:  'Pulse waters these rings outer to inner, one at a time, always '
          + 'the same way round, swinging back dry between them. The order is '
          + 'fixed but the run stops when the target is met, so the number of '
          + 'laps is not known in advance.',
    gentle: 'Gentle sweeps one direction per pass and flips each pass, '
          + 'building depth in many light coats. It keeps going until every '
          + 'ring reaches target, so the number of passes is decided during '
          + 'the run.',
    smooth: 'Smooth chooses the ring order DURING the run, from how much each '
          + 'ring still needs and how the supply is holding up. The rings '
          + 'below are where the water lands; the order it visits them in is '
          + 'not decided until it runs.'
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

  /* The polygon in cartesian mm, projected ONCE.
   *
   * b587: every geometry test used to re-derive each vertex from (deg,
   * throw_mm) with its own sin and cos. Building a 34-ring preview runs
   * pointInZone about 24,000 times (720 half-degree samples per ring), and
   * each call projected all nine vertices again -- around 440,000 sin/cos
   * pairs to draw one picture, all of them the same nine answers. Projecting
   * up front cut a build from 4.9 ms to 0.9 ms, which is what the Zone Setup
   * d-pad was waiting on between steps.
   *
   * The public pointInZone/perimExtent still take a points array and project
   * on the way in, so nothing outside this file has to know. */
  function projectPoly(pts) {
    var n = pts.length, x = new Array(n), y = new Array(n);
    for (var i = 0; i < n; i++) {
      var a = pts[i].deg * Math.PI / 180;
      x[i] = pts[i].throw_mm * Math.sin(a);
      y[i] = pts[i].throw_mm * Math.cos(a);
    }
    return { x: x, y: y, n: n };
  }

  /* Even-odd point-in-polygon in (bearing, throw) space, translated so the
   * test point is the origin -- same method the page used before. */
  function inPoly(P, bearing, r_mm) {
    var px = r_mm * Math.sin(bearing * Math.PI / 180),
        py = r_mm * Math.cos(bearing * Math.PI / 180),
        crosses = 0, n = P.n, X = P.x, Y = P.y;
    for (var i = 0; i < n; i++) {
      var j = (i + 1) % n;
      var y1 = Y[i] - py, y2 = Y[j] - py;
      if ((y1 > 0) !== (y2 > 0)) {
        var x1 = X[i] - px, x2 = X[j] - px;
        var t = y1 / (y1 - y2);
        if (x1 + t * (x2 - x1) > 0) crosses++;
      }
    }
    return (crosses % 2) === 1;
  }
  function pointInZone(pts, bearing, r_mm) {
    return inPoly(projectPoly(pts), bearing, r_mm);
  }

  /* The zone's active arc: the largest gap between vertex bearings is treated
   * as the excluded sector. Three cases force a full 360: all points at one
   * throw (sprinkler centred), the origin inside the polygon, and a long
   * straight edge that merely spans the gap (b435). */
  function zoneArc(pts, P) {
    P = P || projectPoly(pts);
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

    var inside = inPoly(P, 0, 1);
    if (inside) return { start: 0, span: 360, centred: false, originInside: true, gap: mg, gi: gi };

    if (span < 360 && mg > 0) {
      for (var k = 1; k <= 3; k++) {
        var gb = (sdegs[gi] + mg * k / 4) % 360;
        var gbs = Math.sin(gb * Math.PI / 180), gbc = Math.cos(gb * Math.PI / 180), best = 0;
        for (var a = 0; a < P.n; a++) {
          var b = (a + 1) % P.n;
          var ax = P.x[a], ay = P.y[a], bx = P.x[b], by = P.y[b];
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

  /* Per-zone ring coverage, mirroring zone_coverage_scale() in irrigoto.c.
   * 0 Standard, 1 Fine, 2 Finest -- and anything unrecognised is Standard,
   * which is also what a zone saved before coverage existed reports. */
  function coverageScale(c) {
    return c === 2 ? 0.50 : c === 1 ? 0.75 : 1.00;
  }

  /* Ring radii, outer to inner.
   *
   * b561: the 700 mm base pitch is scaled by the zone's coverage, the same
   * way phase_water_zone does through s_ring_pitch_mm. Without this the
   * preview drew Standard spacing whatever the zone was set to, so changing
   * coverage appeared to do nothing -- the preview was claiming a ring layout
   * the firmware would not use. The 80 mm floor is WATER_MIN_RING_SPACING and
   * is NOT scaled, matching the firmware. */
  function ringThrows(pts, actMax, actMin, arc, coverage) {
    var throws = pts.map(function (p) { return p.throw_mm; });
    var zmax = Math.max.apply(null, throws);
    var zmin = (actMin && actMin > 50) ? actMin : Math.min.apply(null, throws);
    var pitch = 700 * coverageScale(coverage);
    var rings = [], t = zmax;
    while (t >= zmin && rings.length < RING_MAX) {
      rings.push(t);
      t -= Math.max(pitch * (t / actMax), 80);
    }
    if (!arc.centred && arc.originInside && zmin > MIN_THROW) {
      while (t > MIN_THROW && rings.length < RING_MAX) {
        rings.push(t);
        t -= Math.max(pitch * (t / actMax), 80);
      }
    }
    return rings;
  }

  /* The wetted spans of one ring: walk the arc and keep what's inside. */
  function ringSpans(pts, arc, thr, P) {
    P = P || projectPoly(pts);
    var spans = [], spanStart = null;
    for (var o = 0; o <= arc.span + STEP_DEG; o += STEP_DEG) {
      var bearing = (arc.start + o) % 360, inside = inPoly(P, bearing, thr);
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

  /* Max ray-polygon intersection distance at a bearing -- the zone's extent
   * there. Mirrors water_perimeter_throw() in irrigoto.c. */
  function extentOf(P, bearing) {
    var bs = Math.sin(bearing * Math.PI / 180), bc = Math.cos(bearing * Math.PI / 180), best = 0;
    for (var i = 0; i < P.n; i++) {
      var j = (i + 1) % P.n;
      var x1 = P.x[i], y1 = P.y[i], x2 = P.x[j], y2 = P.y[j];
      var dx = x2 - x1, dy = y2 - y1, det = bc * dx - bs * dy;
      if (Math.abs(det) < 1e-6) continue;
      var sD = (dx * y1 - dy * x1) / det, u = (bs * y1 - bc * x1) / det;
      if (sD > 0.5 && u >= 0 && u <= 1 && sD > best) best = sD;
    }
    return best;
  }
  function perimExtent(pts, bearing) { return extentOf(projectPoly(pts), bearing); }

  /* The turn between two arcs, for Serpentine and Sections.
   *
   * b550: the preview used to draw nothing here, or a grey dotted radial
   * line, and that is why Serpentine looked like "a different path approach
   * entirely" next to the real thing. The firmware never shuts the stream
   * off at a turn (serpentine_build_pass_plan, b426): when the direct glide
   * would leave the polygon, the path rides just inside the boundary
   * instead, watering the whole way. Those boundary-hug waypoints are the
   * majority of the legs in a real plan, so leaving them out of the picture
   * left out most of the path.
   *
   * Mirrors the firmware exactly: ~4 deg waypoints, radius clamped to
   * min(linear glide, perimeter extent - 200 mm), each one point-in-polygon
   * verified. A single failure means a true exclusion, and the whole turn
   * falls back to the firmware's dry hop.
   */
  var TURN_STEP_DEG = 4.0, TURN_MARGIN_MM = 200.0, TURN_MAX_WPS = 64;

  function buildTurn(P, fromB, fromR, toB, toR, turnFloor) {
    var d = toB - fromB;
    while (d >  180) d -= 360;
    while (d < -180) d += 360;
    var nw = Math.floor(Math.abs(d) / TURN_STEP_DEG) + 1;
    if (nw > TURN_MAX_WPS) nw = TURN_MAX_WPS;
    var wps = [];
    for (var wi = 1; wi <= nw; wi++) {
      var f = wi / nw;
      var b = ((fromB + d * f) % 360 + 360) % 360;
      var r = fromR + (toR - fromR) * f;
      var ext = extentOf(P, b) - TURN_MARGIN_MM;
      if (ext < r) r = ext;
      if (!inPoly(P, b, r)) r -= TURN_MARGIN_MM;          /* one nudge inward */
      if (r < turnFloor || !inPoly(P, b, r)) return { wet: false };
      wps.push({ deg: b, r: (wi === nw) ? toR : r });
    }
    return { wet: true, wps: wps };
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

    var cw = [];
    for (i = 0; i < nRings; i++) {
      var d;
      if (sequential)                    d = (i % 2) === 0;
      else if (serpish)                  d = (i % 2) === 0;
      else if (modeKey === 'pulse')      d = true;
      else                               d = (pass % 2) === 0;  /* gentle, smooth */
      cw.push(d);
    }
    /* b564: dryReturn used to be computed here and consumed by draw(). It was
     * a guess -- "same direction as the last ring, so it must travel back
     * dry" -- and it was wrong for Serpentine and Sections, where the
     * firmware rides the boundary with the stream ON. Whether a connector is
     * wet is now decided in build() by the same point-in-polygon test the
     * firmware uses, per connector, not inferred from sweep direction. */
    return { idx: idx, cw: cw, orderVaries: (modeKey === 'smooth' && !sequential) };
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

    /* b578: order the lobes by how far OUT they reach, mirroring the
     * firmware. The lobe list comes from ringSpans(), which walks the zone
     * arc, so it is in BEARING order -- and b575 changed the firmware to
     * process lobes by reach instead, so the two disagreed about which side
     * to start on. That is the reported "it started on the opposite side of
     * what preview showed". The rule is the same one the planner uses: the
     * lobe containing the outermost ring goes first. */
    var order = [], li;
    for (li = 0; li < lobes.length; li++) {
        var edge = ringsIn.length;               /* outermost ring index in this lobe */
        for (var ri2 = 0; ri2 < ringsIn.length; ri2++) {
            var hit = false;
            for (var sj = 0; sj < ringsIn[ri2].spans.length; sj++)
                if (spanOverlaps(ringsIn[ri2].spans[sj], lobes[li])) { hit = true; break; }
            if (hit) { edge = ri2; break; }
        }
        order.push({ lobe: lobes[li], edge: edge });
    }
    order.sort(function (a, b) { return a.edge - b.edge; });
    lobes = order.map(function (o) { return o.lobe; });

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

    var P = projectPoly(points);            /* b587: project once, reuse */
    var arc = zoneArc(points, P);

    /* b587: when the firmware has published its own ring ladder, DRAW THAT.
     *
     * This page rebuilt the ladder from the zone outline and the two did not
     * come out the same -- 34 rings here against the firmware's 31, because
     * the inner-ring rule differs at the bottom of the range. So the modal
     * showed "10 of 31 rings" in the plan note and "34 rings" under the
     * picture, and the preview drew three rings the run would never water.
     * b573 papered over the consequence by matching pass counts on radius;
     * the cause was having two ladders at all.
     *
     * ring_mm IS the ladder the run will sweep, so there is nothing left to
     * derive. A ring the firmware scheduled for 0 passes is unwaterable and
     * is dropped rather than drawn as an arc that never animates.
     *
     * ringThrows() stays as the fallback: Zone Setup has no run to ask about,
     * and neither does a schedule card before its plan arrives. */
    var rpIn = opts.ringPasses, rmIn = opts.ringMm;
    var thr = null, fwPasses = null;
    if (rmIn && rmIn.length && rpIn && rpIn.length === rmIn.length) {
      thr = []; fwPasses = [];
      for (var fq = 0; fq < rmIn.length; fq++) {
        if (!(rmIn[fq] > 0) || !(rpIn[fq] > 0)) continue;
        thr.push(rmIn[fq]); fwPasses.push(rpIn[fq]);
      }
      if (!thr.length) { thr = null; fwPasses = null; }
    }
    if (!thr) thr = ringThrows(points, actMax, actMin, arc, opts.coverage | 0);
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
        spans: ringSpans(points, arc, thr[ri], P)
      });
    }
    /* b540: Sections reorders the visits lobe-major. Without this the preview
     * drew Sections and Serpentine identically -- it claimed a behaviour the
     * firmware has but the preview never modelled. */
    var sectioned = (modeKey === 'sections') ? orderSections(rings) : null;
    if (sectioned) rings = sectioned;

    /* b564: the moves the nozzle actually makes, in order, including what it
     * does BETWEEN arcs. Serpentine and Sections never shut the stream off at
     * a turn -- serpentine_build_pass_plan rides the polygon boundary instead
     * (b426) -- and those boundary-hug waypoints are the majority of the legs
     * in a real plan. Drawing only the ring arcs left most of the path out,
     * which is why Serpentine looked like "a different path approach
     * entirely" next to the real thing.
     *
     * Every other mode closes the valve between arcs, which is what the
     * shared ring loop does ("closing valve for nozzle transit"), so their
     * connectors stay dry. */
    var serpish = (modeKey === 'serpentine' || modeKey === 'sections');
    var turnFloor = Math.max(actMin || 0, 500);

    /* b569: the WHOLE run, not one representative lap.
     *
     * ringPasses[i] is how many passes ring i needs, from /api/run_plan --
     * the firmware solves it from the deposit each slow pass achieves. Pass 1
     * sweeps every ring; pass k sweeps only the rings still short, which is
     * exactly what skip[] does in water_serpentine_passes, so a four-pass run
     * is one full lap plus three progressively smaller ones rather than four
     * laps. Serpentine and Sections alternate direction per pass
     * (out_to_in = pass % 2), so later passes are drawn running the other way
     * round, as they will.
     *
     * With no ringPasses (or a length that does not line up with the ring
     * ladder) this degrades to the single pass it always drew, rather than
     * inventing a run. */
    /* b587: Zone Setup draws rings only and throws the move list away, but
     * build() computed it anyway -- boundary-hug turns are point-in-polygon
     * tested per 4 deg waypoint, so a 34-ring zone spent most of a 5 ms
     * build on 139 moves nobody drew. That ran on every d-pad step, on a
     * page whose responsiveness was the original complaint. Skip it. */
    if (opts.ringsOnly) {
      return {
        moves: [], modeKey: modeKey,
        modeLabel: (MODES[opts.mode] || MODES['1']).label,
        arc: arc, lobes: 1, rings: rings, ringCount: thr.length, passes: 1,
        coverage: opts.coverage | 0,
        scale_mm: opts.scale_mm || (actMax + 914),
        orderVaries: plan.orderVaries, noPath: false
      };
    }

    /* b587: the ladder IS the firmware's now (see above), so a ring's pass
     * count is just its own entry -- no radius matching, no tolerance, no
     * way for the two to line up wrongly. b573's nearest-radius search
     * existed only to reconcile two ladders that should never have been
     * built separately. */
    var passFor = function (idx) {
      return fwPasses ? (fwPasses[rings[idx].ring] || 1) : 1;
    };
    var nPasses = 1;
    if (fwPasses) fwPasses.forEach(function (v) { if (v > nPasses) nPasses = v; });
    if (nPasses > 12) nPasses = 12;        /* a drawing, not an endurance test */

    var moves = [], prev = null;
    for (var pp = 0; pp < nPasses; pp++) {
      /* Rings still owed water on this pass. */
      var live = [];
      for (var q = 0; q < rings.length; q++) {
        var need = passFor(q);
        if (need > pp) live.push(rings[q]);
      }
      if (!live.length) break;
      /* Later passes of serpentine/sections run the other way round. */
      if (pp % 2 === 1 && serpish) live = live.slice().reverse();

      for (var mi = 0; mi < live.length; mi++) {
        var MR = live[mi];
        /* Direction flips per pass for the serpentine family. */
        var cwp = (pp % 2 === 1 && serpish) ? !MR.cw : MR.cw;
        var ord = MR.spans.slice().sort(function (a, b) {
          return cwp ? (a.lo - b.lo) : (b.lo - a.lo);
        });
        for (var mj = 0; mj < ord.length; mj++) {
          var msp = ord[mj];
          var mfrom = cwp ? msp.lo : (msp.lo + msp.span);
          var mto   = cwp ? (msp.lo + msp.span) : msp.lo;
          if (prev) {
            var turn = serpish
              ? buildTurn(P, prev.deg, prev.r, mfrom, MR.throw_mm, turnFloor)
              : { wet: false };
            if (turn.wet) moves.push({ type: 'turn', from: prev, wps: turn.wps });
            else moves.push({ type: 'hop', from: prev,
                              to: { deg: mfrom, r: MR.throw_mm } });
          }
          moves.push({ type: 'sweep', ring: MR.ring, visit: mi, pass: pp,
                       r: MR.throw_mm, from: mfrom, to: mto, cw: cwp });
          prev = { r: MR.throw_mm, deg: mto };
        }
      }
    }

    return {
      moves: moves,
      modeKey: modeKey,
      modeLabel: (MODES[opts.mode] || MODES['1']).label,
      arc: arc,
      lobes: sectioned ? (sectioned[sectioned.length - 1].lobe + 1) : 1,
      rings: rings,
      /* b587: distinct rings, not ring VISITS. Sections visits each ring
       * once per lobe, so rings.length is 42 where the ladder has 30, and
       * the captions were reporting that as a ring count -- next to a plan
       * note from the firmware that said 31. */
      ringCount: thr.length,
      passes: nPasses,
      coverage: opts.coverage | 0,
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

    /* b569: ringsOnly draws WHERE the water lands and nothing about HOW the
     * nozzle gets there -- no direction colouring, no arrows, no connectors,
     * no marker. Zone Setup uses it: at that point the zone and its ring
     * spacing are the subject, and the sweep order belongs to the run. */
    var ringsOnly = !!o.ringsOnly;
    for (var i = 0; i < n; i++) {
      var R = geom.rings[i];
      var r = (R.throw_mm / scale) * maxR;
      if (!(r > 0) || r > maxR * 1.02) continue;
      /* Fade with visit order so the sequence reads without a legend. */
      var al = 0.75 - 0.45 * (i / (n - 1 || 1));
      var col = ringsOnly ? 'rgba(80,180,255,.55)'
              : R.cw ? 'rgba(80,180,255,' + al + ')' : 'rgba(255,160,60,' + al + ')';
      for (var s = 0; s < R.spans.length; s++) {
        var sp = R.spans[s], sa = rad(sp.lo);
        ctx.beginPath();
        ctx.arc(cx, cy, r, sa, sa + sp.span * Math.PI / 180, false);
        ctx.strokeStyle = col;
        ctx.lineWidth = thin ? 1.2 : 1.8;
        ctx.setLineDash([]);
        ctx.stroke();
        if (!thin && !ringsOnly && sp.span > 8) arrow(ctx, cx, cy, r, sp, R.cw);
      }
    }

    /* b564: connectors, from the same ordered move list the scrubber walks --
     * so the picture and the animation cannot disagree. A wet boundary-hug
     * turn (Serpentine/Sections) is drawn solid, because the stream is on for
     * all of it; a dry hop stays dotted grey. The old code inferred a dry
     * return from a dryReturn flag on the ring and drew a straight radial
     * line, which was wrong for both: it never drew the boundary-hug turns at
     * all, and it drew a straight chord where the nozzle rides an arc. */
    if (ringsOnly || !geom.moves) return;
    geom.moves.forEach(function (mv) {
      if (mv.type === 'turn') {
        ctx.beginPath();
        ctx.moveTo(cx + Math.cos(rad(mv.from.deg)) * (mv.from.r / scale) * maxR,
                   cy + Math.sin(rad(mv.from.deg)) * (mv.from.r / scale) * maxR);
        mv.wps.forEach(function (w) {
          ctx.lineTo(cx + Math.cos(rad(w.deg)) * (w.r / scale) * maxR,
                     cy + Math.sin(rad(w.deg)) * (w.r / scale) * maxR);
        });
        ctx.strokeStyle = 'rgba(80,180,255,.42)';
        ctx.lineWidth = thin ? 1.0 : 1.4;
        ctx.setLineDash([]);
        ctx.stroke();
      } else if (mv.type === 'hop' && !thin) {
        ctx.beginPath();
        ctx.moveTo(cx + Math.cos(rad(mv.from.deg)) * (mv.from.r / scale) * maxR,
                   cy + Math.sin(rad(mv.from.deg)) * (mv.from.r / scale) * maxR);
        ctx.lineTo(cx + Math.cos(rad(mv.to.deg)) * (mv.to.r / scale) * maxR,
                   cy + Math.sin(rad(mv.to.deg)) * (mv.to.r / scale) * maxR);
        ctx.strokeStyle = 'rgba(150,150,150,.35)';
        ctx.lineWidth = 1;
        ctx.setLineDash([2, 4]);
        ctx.stroke();
        ctx.setLineDash([]);
      }
    });
  }

  /* Flatten the plan into the ordered list of moves the nozzle makes, so a
   * scrubber can walk it. Wet segments are ring arcs; dry segments are the
   * valve-closed hops between them. Lengths are real distances in mm, so the
   * dot moves at a believable speed rather than jumping ring to ring. */
  function flatten(geom) {
    if (!geom || geom.noPath || !geom.moves || !geom.moves.length)
      return { segs: [], total: 0 };
    var segs = [];
    geom.moves.forEach(function (mv) {
      if (mv.type === 'sweep') {
        /* b587: the span, NOT angDelta. build() writes from/to as lo and
         * lo+span without wrapping, so their difference IS the arc; angDelta
         * folds anything over 180 back into +/-180 and a 237 deg lobe was
         * timed as 123. A full 360 ring -- a centred sprinkler, the common
         * case for a round bed -- folded to exactly 0 and fell through to the
         * 1 mm floor, so the marker crossed the whole ring in one frame while
         * the run spends a minute on it. The picture was right and the
         * playback was not, which is the one thing the play button is for. */
        var len = mv.r * (Math.abs(mv.to - mv.from) * Math.PI / 180);
        segs.push({ dry: false, ring: mv.ring, visit: mv.visit, pass: mv.pass || 0,
                    r: mv.r, from: mv.from, to: mv.to, cw: mv.cw,
                    len: Math.max(len, 1) });
      } else if (mv.type === 'turn') {
        /* Wet, and followed waypoint by waypoint so the scrubber traces the
         * boundary the way the glide engine does. */
        var pr = mv.from.r, pd = mv.from.deg;
        mv.wps.forEach(function (w) {
          var dl = polarDist(pr, pd, w.r, w.deg);
          if (dl > 1) segs.push({ dry: false, turn: true, ring: -1, r: w.r,
                                  r0: pr, d0: pd, r1: w.r, d1: w.deg,
                                  from: pd, to: w.deg, len: dl });
          pr = w.r; pd = w.deg;
        });
      } else {
        var dh = polarDist(mv.from.r, mv.from.deg, mv.to.r, mv.to.deg);
        if (dh > 1) segs.push({ dry: true, r0: mv.from.r, d0: mv.from.deg,
                                r1: mv.to.r, d1: mv.to.deg, len: dh });
      }
    });
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
        if (sg.dry || sg.turn) {
          return { r_mm: sg.r0 + (sg.r1 - sg.r0) * f,
                   bearing: sg.d0 + angDelta(sg.d0, sg.d1) * f,
                   dry: !!sg.dry, turn: !!sg.turn, ring: -1 };
        }
        return { r_mm: sg.r, bearing: sg.from + (sg.to - sg.from) * f,
                 dry: false, ring: sg.ring, visit: sg.visit,
                 pass: sg.pass || 0, cw: sg.cw };
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
  /* b570: where the nozzle actually is, from /api/status, drawn on top of the
   * planned path. This is the point of closing the loop between preview and
   * run: the same picture, with the real thing moving over it. A stale
   * reading (the nozzle transiting between arcs, where no sample is taken)
   * is drawn hollow rather than frozen solid, so a stopped dot never reads
   * as a live one. */
  function drawLive(ctx, live, o) {
    if (!live || !(live.throw_mm > 0)) return;
    var r = (live.throw_mm / o.scale_mm) * o.maxR;
    if (!(r > 0) || r > o.maxR * 1.05) return;
    var a = rad(live.deg);
    var x = o.cx + Math.cos(a) * r, y = o.cy + Math.sin(a) * r;
    var fresh = (live.age_ms || 0) < 2500;
    ctx.beginPath();
    ctx.arc(x, y, fresh ? 5 : 4, 0, Math.PI * 2);
    if (fresh) {
      ctx.fillStyle = 'rgba(120,255,180,.95)';
      ctx.fill();
      ctx.beginPath(); ctx.arc(x, y, 9, 0, Math.PI * 2);
      ctx.strokeStyle = 'rgba(120,255,180,.45)';
    } else {
      ctx.strokeStyle = 'rgba(120,255,180,.5)';
    }
    ctx.lineWidth = 1.5; ctx.setLineDash([]); ctx.stroke();
  }

  /* b587: returns the geometry it drew (or null).
   *
   * Every caller was doing thumb(cv, pts, o) and then build(pts, o) again to
   * read the ring count for the caption -- the same geometry computed twice
   * per card, and two chances to pass slightly different opts and caption a
   * picture with another picture's numbers. Nothing used the old return
   * value (a marker point, or `true`), so handing back the geometry costs
   * nothing and lets callers stop rebuilding. */
  function thumb(canvas, points, opts) {
    if (!canvas || !canvas.getContext) return null;
    var ctx = canvas.getContext('2d');
    var W = canvas.width, H = canvas.height;
    ctx.clearRect(0, 0, W, H);
    var cx = W / 2, cy = H / 2, maxR = Math.min(W, H) / 2 - 2;

    var css = getComputedStyle(document.documentElement);
    var bg = (css.getPropertyValue('--radar-bg') || '#0a1a12').trim();
    ctx.fillStyle = bg;
    ctx.beginPath(); ctx.arc(cx, cy, maxR, 0, Math.PI * 2); ctx.fill();

    points = normPoints(points);
    if (points.length < 2) return null;
    var geom = build(points, opts);
    if (!geom) return null;

    /* Fit the thumbnail to the zone; full reach wastes most of the disc.
     * b587: this scale is no longer stashed anywhere. b571 kept it in a
     * module-level _lastThumbScale so the overlay could draw the live dot in
     * a second pass at the same scale; that pass is gone -- thumb() draws it
     * itself below, from the scale still in hand. (The original bug b571
     * fixed was stashing it on `ov`, which is null until the overlay first
     * opens, so the Water modal threw on open and the button did nothing.) */
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
      return geom;
    }
    var dopt = { cx: cx, cy: cy, maxR: maxR, scale_mm: scale, thumb: !!opts.thumb,
                 ringsOnly: !!opts.ringsOnly };
    draw(ctx, geom, dopt);
    /* Optional scrub marker (opts.scrub is 0..1). */
    if (typeof opts.scrub === 'number') marker(ctx, flatten(geom), opts.scrub, dopt);
    /* Sprinkler at the centre. */
    ctx.fillStyle = 'rgba(0,232,122,.9)';
    ctx.beginPath(); ctx.arc(cx, cy, 2, 0, Math.PI * 2); ctx.fill();

    if (opts.live) drawLive(ctx, opts.live,
                            { cx: cx, cy: cy, maxR: maxR, scale_mm: scale });
    return geom;
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
        '<button class="ipo-btn ipo-x" aria-label="Close">&#10005;</button>' +
      '</div>' +
      '<canvas class="ipo-cv" width="620" height="620"></canvas>' +
      '<div class="ipo-scrub">' +
        '<button class="ipo-btn ipo-play" aria-label="Play">&#9654;</button>' +
        '<input type="range" min="0" max="1000" step="1" value="0" aria-label="Position along the path">' +
      '</div>' +
      /* b594: the LIVE bar. Not a control -- a readout. A run in progress has
       * nothing to scrub to, so the transport row is hidden and this takes
       * its place: how far through the run the device actually is. */
      '<div class="ipo-prog"><div class="ipo-prog-fill"></div></div>' +
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
      '#irr-path-ov .ipo-note{width:min(92vw,620px);font-size:11px;' +
        'color:var(--text-mid);text-align:center;min-height:14px;}' +
      '#irr-path-ov .ipo-prog{display:none;width:min(92vw,620px);height:6px;' +
        'background:var(--btn);border:1px solid var(--border);border-radius:4px;' +
        'overflow:hidden;}' +
      '#irr-path-ov .ipo-prog.on{display:block;}' +
      '#irr-path-ov .ipo-prog-fill{height:100%;width:0;background:var(--green);' +
        'box-shadow:0 0 8px var(--green-glow);transition:width .4s linear;}';
    document.head.appendChild(css);
    document.body.appendChild(el);
    ov = {
      el: el,
      cv: el.querySelector('.ipo-cv'),
      title: el.querySelector('.ipo-title'),
      modes: el.querySelector('.ipo-modes'),
      play: el.querySelector('.ipo-play'),
      range: el.querySelector('input'),
      scrub: el.querySelector('.ipo-scrub'),
      prog: el.querySelector('.ipo-prog'),
      progFill: el.querySelector('.ipo-prog-fill'),
      note: el.querySelector('.ipo-note'),
      opts: null, mode: '7', t: 0, timer: null
    };
    el.addEventListener('click', function (e) { if (e.target === el) ovClose(); });
    ov.el.querySelector('.ipo-x').addEventListener('click', ovClose);
    ov.range.addEventListener('input', function () { ov.t = +ov.range.value / 1000; ovDraw(); });
    ov.play.addEventListener('click', ovPlay);
    document.addEventListener('keydown', function (e) { if (e.key === 'Escape') ovClose(); });
    return ov;
  }

  /* Playback pace.
   *
   * b569 made the scrubber walk the WHOLE run, but the timer still finished
   * in a flat ~8 s, so a two-pass run played at double speed and a six-pass
   * run at six times -- the longer the real run, the faster the preview of
   * it. The play button is there to show what will happen, and the one thing
   * it was reliably wrong about was how long.
   *
   * Pace it by the drawn path instead: a nominal 7 s for a single lap,
   * growing with the number of passes, capped at 20 s so a long run is still
   * watchable. Not real time -- a real run is minutes -- but monotonic in
   * run length, so two runs compare the way they actually will.
   */
  var PLAY_BASE_MS = 7000, PLAY_MAX_MS = 20000, PLAY_TICK_MS = 33;

  function ovPlayMs() {
    var p = (ov.opts && ov.opts.ringPasses) || null, mx = 1;
    if (p && p.length) for (var i = 0; i < p.length; i++) if (p[i] > mx) mx = p[i];
    return Math.min(PLAY_MAX_MS, PLAY_BASE_MS * Math.sqrt(mx));
  }

  function ovPlay() {
    if (ov.timer) { clearInterval(ov.timer); ov.timer = null; ov.play.innerHTML = '&#9654;'; return; }
    if (ov.t >= 1) ov.t = 0;          /* replay from the start, not stuck at the end */
    ov.play.innerHTML = '&#9632;';
    var step = PLAY_TICK_MS / ovPlayMs();
    ov.timer = setInterval(function () {
      ov.t += step;
      if (ov.t >= 1) { ov.t = 1; clearInterval(ov.timer); ov.timer = null; ov.play.innerHTML = '&#9654;'; }
      ov.range.value = Math.round(ov.t * 1000);
      ovDraw();
    }, PLAY_TICK_MS);
  }

  function ovClose() {
    if (!ov) return;
    if (ov.timer) { clearInterval(ov.timer); ov.timer = null; ov.play.innerHTML = '&#9654;'; }
    /* b594: drop the live handle. Without this a later preview of the same
     * zone reopens still holding the last run's position and progress. */
    if (ov.opts) { ov.opts.live = null; ov.opts.liveMode = false; }
    ov.prog.classList.remove('on');
    ov.el.classList.remove('open');
  }

  function ovDraw() {
    var o = {
      /* pass 0: build() walks every pass itself from ringPasses, so the
       * per-pass variant this used to select no longer exists. */
      mode: ov.mode, pass: 0, scrub: ov.t,
      act_max_throw: ov.opts.act_max_throw, act_min_throw: ov.opts.act_min_throw,
      /* b564: without this the overlay drew Standard ring spacing whatever
       * the zone was set to -- the Sections preview in particular looked
       * unaffected by coverage because the lobe ordering runs on top of a
       * ring list that was built at the wrong pitch. */
      coverage: ov.opts.coverage | 0,
      /* b569: the scrubber now walks the WHOLE run, every pass, so there is
       * nothing left for a pass stepper to step through. */
      ringPasses: ov.opts.ringPasses,
      ringMm: ov.opts.ringMm,
      /* b587: hand the live position to thumb() instead of drawing it again
       * afterwards. The second call re-derived the same centre and radius
       * from the canvas and reused _lastThumbScale to get back the scale
       * thumb() had just computed -- two places that had to stay in step
       * about where the disc is, for one dot. */
      live: ov.opts.live
    };
    /* b594: a run in progress has nothing to scrub to -- the answer to "where
     * is it" is the live dot, not a slider the viewer drags. Suppress the
     * planned marker while live. */
    if (ov.opts.liveMode) delete o.scrub;
    thumb(ov.cv, ov.opts.points, o);
    var label = (MODES[ov.mode] || {}).label || '';
    ov.title.textContent = (ov.opts.title ? ov.opts.title + ' \u00b7 ' : '') + label;
    ovChrome();
    /* b572: no per-frame readout here. It was a monospace span in the same
     * flex row as the scrubber, and once b569 added the pass number the text
     * outgrew its min-width and resized the slider as playback moved -- the
     * bar jittered under the thumb. The marker on the canvas already shows
     * where the nozzle is. */
  }

  /* b594: show the controls the situation actually supports.
   *
   * Three different questions get asked of this overlay, and they were all
   * being answered with the same transport bar:
   *
   *   1. Zone Setup          where does the water land? (rings only, no
   *                          overlay -- drawn inline on that page)
   *   2. Preview, before a run
   *        deterministic     scrub + play: this is the run that WILL happen
   *        adaptive          path + a sentence: the order is decided at run
   *                          time, so there is nothing truthful to animate
   *   3. Live, during a run  progress + the real dot. A slider here invites
   *                          dragging to a position the device is not at.
   */
  function ovChrome() {
    var mk = (MODES[ov.mode] || MODES['1']).key;
    var det = isDeterministic(mk);
    var live = !!ov.opts.liveMode;

    /* Transport: only for a deterministic preview. */
    var showScrub = det && !live;
    ov.scrub.style.display = showScrub ? 'flex' : 'none';
    if (!showScrub && ov.timer) {
      clearInterval(ov.timer); ov.timer = null; ov.play.innerHTML = '&#9654;';
    }

    /* Progress: only while live, and only when we can honestly compute it. */
    var frac = live ? liveFraction() : -1;
    ov.prog.classList.toggle('on', frac >= 0);
    if (frac >= 0) ov.progFill.style.width = (frac * 100).toFixed(1) + '%';

    ov.note.textContent = live ? liveNote(mk, frac)
                        : det   ? ''
                                : (ADAPTIVE_NOTE[mk] || '');
  }

  /* How far through the run, 0..1, or -1 when it cannot be said honestly.
   *
   * Deterministic modes know their total pass count up front, so
   * (passes done + progress through this pass) / total is a real fraction.
   * Adaptive modes do not know how many passes they will make, so there is no
   * denominator -- reporting a percentage would be inventing one. */
  function liveFraction() {
    var L = ov.opts.live;
    if (!L) return -1;
    var mk = (MODES[ov.mode] || MODES['1']).key;
    if (!isDeterministic(mk)) return -1;
    var total = L.passes_total | 0, rings = L.rings_total | 0;
    if (total < 1 || rings < 1) return -1;
    var pass = Math.max(0, (L.pass | 0) - 1);      // live_pass is 1-based
    var ring = Math.max(0, L.ring | 0);
    var f = (pass + Math.min(1, ring / rings)) / total;
    return Math.max(0, Math.min(1, f));
  }

  function liveNote(mk, frac) {
    var L = ov.opts.live || {};
    var bits = [];
    if (L.pass > 0 && L.passes_total > 0)
      bits.push('Pass ' + L.pass + ' of ' + L.passes_total);
    if (L.ring >= 0 && L.rings_total > 0)
      bits.push('ring ' + ((L.ring | 0) + 1) + ' of ' + L.rings_total);
    if (frac >= 0) bits.push((frac * 100).toFixed(0) + '% through');
    else if (!isDeterministic(mk))
      bits.push('this mode decides its passes as it runs, so there is no '
              + 'total to count against');
    if ((L.age_ms || 0) > 2500) bits.push('moving between arcs');
    return bits.join(' \u00b7 ');
  }

  function openPreview(opts) {
    if (!opts) return false;
    var pts = normPoints(opts.points);
    if (pts.length < 2) return false;
    ovBuild();
    ov.opts = opts;
    ov.opts.points = pts;
    ov.mode = MODES[opts.mode] ? opts.mode : '7';
    ov.t = 0; ov.range.value = 0;
    if (ov.timer) { clearInterval(ov.timer); ov.timer = null; }
    ov.play.innerHTML = '&#9654;';
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
    flatten: flatten, pointAt: pointAt, marker: marker, drawLive: drawLive,
    /* b582: feed new live data to an open overlay. Returns false when the
     * overlay is closed, so the caller can stop polling for it. */
    updateLive: function (live) {
      if (!ov || !ov.el || !ov.el.classList.contains('open')) return false;
      ov.opts.live = live;
      ovDraw();
      return true;
    },
    isOpen: function () {
      return !!(ov && ov.el && ov.el.classList.contains('open'));
    },
    zoneArc: zoneArc, ringThrows: ringThrows, ringSpans: ringSpans,
    pointInZone: pointInZone
  };
})(window);
)PATHJS"
#endif /* IRRIGOTO_HTML_PAYLOAD */
