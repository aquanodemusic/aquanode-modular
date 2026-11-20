#pragma once
// The MPE Piano Roll's notes: a pitch path of points joined by lines, bends
// (quadratic Beziers) or eases, an expression line, and the Join rules.
// (plain C++, so the offline test reads it as it is.)
#include <cmath>
#include <vector>
#include <algorithm>

template <typename T> inline T mperollClamp (T lo, T hi, T v) { return v < lo ? lo : (v > hi ? hi : v); }

struct MpeRollPoint { double t { 0.0 }; float pitch { 60.0f }; };   // t: beats from the note's start

struct MpeRollSegment
{
    enum Kind { Line = 0, Bend = 1, Ease = 2 };
    int kind { Line };
    double ct { 0.0 };        // Bend: the control point (beats from the note's start, pitch)
    float cp { 60.0f };
};

struct MpeRollNote
{
    double start { 0.0 };                       // beats from the loop's start
    std::vector<MpeRollPoint> points;           // at least two, t rising, points[0].t == 0
    std::vector<MpeRollSegment> segments;       // points.size() - 1
    std::vector<std::pair<float, float>> expr;  // (0..1 along the note, value 0..1), at least two
    float velocity { 0.8f };

    double length() const { return points.empty() ? 0.0 : points.back().t; }
    double end() const { return start + length(); }
    float pitchAt (double t) const;             // t: beats from the note's start
    float exprAt (double t) const;
    bool isValid() const { return points.size() >= 2 && segments.size() == points.size() - 1 && length() > 1.0e-6; }
};


//==============================================================================
// a note's shape
//==============================================================================
namespace mperoll_detail
{
    // a bent segment: a quadratic Bezier from (t0, p0) over (ct, cp) to (t1, p1),
    // read at time t (its control's time kept inside the segment, so time only rises)
    inline float bezierAt (double t0, float p0, double ct, float cp, double t1, float p1, double t)
    {
        ct = mperollClamp (t0, t1, ct);
        double lo = 0.0, hi = 1.0;
        for (int i = 0; i < 24; ++i)
        {
            const double s = 0.5 * (lo + hi);
            const double x = (1 - s) * (1 - s) * t0 + 2 * (1 - s) * s * ct + s * s * t1;
            (x < t ? lo : hi) = s;
        }
        const double s = 0.5 * (lo + hi);
        return (float) ((1 - s) * (1 - s) * p0 + 2 * (1 - s) * s * cp + s * s * p1);
    }

    inline float segmentAt (const MpeRollPoint& a, const MpeRollPoint& b, const MpeRollSegment& seg, double t)
    {
        const double span = b.t - a.t;
        if (span <= 1.0e-9) return b.pitch;
        const double u = mperollClamp (0.0, 1.0, (t - a.t) / span);
        switch (seg.kind)
        {
            case MpeRollSegment::Bend: return bezierAt (a.t, a.pitch, seg.ct, seg.cp, b.t, b.pitch, t);
            case MpeRollSegment::Ease: { const double e = u * u * (3.0 - 2.0 * u); return (float) (a.pitch + (b.pitch - a.pitch) * e); }
            default:                   return (float) (a.pitch + (b.pitch - a.pitch) * u);
        }
    }
}

inline float MpeRollNote::pitchAt (double t) const
{
    if (points.empty()) return 60.0f;
    if (t <= points.front().t) return points.front().pitch;
    if (t >= points.back().t) return points.back().pitch;
    for (size_t i = 0; i + 1 < points.size(); ++i)
        if (t <= points[i + 1].t)
            return mperoll_detail::segmentAt (points[i], points[i + 1], i < segments.size() ? segments[i] : MpeRollSegment(), t);
    return points.back().pitch;
}

inline float MpeRollNote::exprAt (double t) const
{
    if (expr.empty()) return 0.7f;
    const double len = length();
    const float u = (float) (len > 0.0 ? mperollClamp (0.0, 1.0, t / len) : 0.0);
    if (u <= expr.front().first) return expr.front().second;
    for (size_t i = 0; i + 1 < expr.size(); ++i)
        if (u <= expr[i + 1].first)
        {
            const float span = expr[i + 1].first - expr[i].first;
            const float f = span > 1.0e-6f ? (u - expr[i].first) / span : 1.0f;
            return expr[i].second + (expr[i + 1].second - expr[i].second) * f;
        }
    return expr.back().second;
}

namespace mperoll_detail
{
    // a note's part from t0 to t1 (beats from its start), as points and segments in
    // absolute time, appended to a growing note (its own expression sampled alongside)
    struct Builder
    {
        double start { 0.0 };
        std::vector<MpeRollPoint> pts;            // absolute time here
        std::vector<MpeRollSegment> segs;         // (control times absolute)
        std::vector<std::pair<double, float>> ex; // absolute time, value

        void addPoint (double t, float p, MpeRollSegment seg)
        {
            if (! pts.empty() && t <= pts.back().t + 1.0e-9)
            {
                // the same instant: the newer pitch wins
                pts.back().pitch = p;
                return;
            }
            if (! pts.empty()) segs.push_back (seg);
            pts.push_back ({ t, p });
        }

        void addPart (const MpeRollNote& n, double t0, double t1, bool first, MpeRollSegment bridge = {})
        {
            t0 = mperollClamp (0.0, n.length(), t0);
            t1 = mperollClamp (t0, n.length(), t1);
            // the first point of the part (reached over the bridge)
            addPoint (n.start + t0, n.pitchAt (t0), first ? MpeRollSegment() : bridge);
            for (size_t i = 0; i < n.points.size(); ++i)
            {
                const double t = n.points[i].t;
                if (t <= t0 + 1.0e-9 || t >= t1 - 1.0e-9) continue;
                MpeRollSegment s = i > 0 && i - 1 < n.segments.size() ? n.segments[i - 1] : MpeRollSegment();
                if (s.kind == MpeRollSegment::Bend) s.ct += n.start;
                addPoint (n.start + t, n.points[i].pitch, s);
            }
            // the last point, on the segment it falls in
            MpeRollSegment last;
            for (size_t i = 0; i + 1 < n.points.size(); ++i)
                if (t1 <= n.points[i + 1].t + 1.0e-9) { last = n.segments[i]; if (last.kind == MpeRollSegment::Bend) last.ct += n.start; break; }
            if (last.kind == MpeRollSegment::Bend && (last.ct < n.start + t0 || last.ct > n.start + t1)) last.kind = MpeRollSegment::Line;
            addPoint (n.start + t1, n.pitchAt (t1), last);
            const int steps = 16;
            for (int k = 0; k <= steps; ++k)
            {
                const double t = t0 + (t1 - t0) * k / steps;
                ex.push_back ({ n.start + t, n.exprAt (t) });
            }
        }

        bool finish (float velocity, MpeRollNote& out)
        {
            if (pts.size() < 2) return false;
            out = MpeRollNote();
            out.start = pts.front().t;
            out.velocity = velocity;
            for (auto& p : pts) out.points.push_back ({ p.t - out.start, p.pitch });
            for (auto s : segs) { if (s.kind == MpeRollSegment::Bend) s.ct -= out.start; out.segments.push_back (s); }
            const double len = out.length();
            if (len <= 1.0e-6) return false;
            std::sort (ex.begin(), ex.end(), [] (const auto& a, const auto& b) { return a.first < b.first; });
            for (auto& e : ex)
            {
                const float u = (float) mperollClamp (0.0, 1.0, (e.first - out.start) / len);
                if (! out.expr.empty() && u <= out.expr.back().first + 1.0e-5f) { out.expr.back().second = e.second; continue; }
                out.expr.push_back ({ u, e.second });
            }
            if (out.expr.size() < 2) out.expr = { { 0.0f, 0.7f }, { 1.0f, 0.7f } };
            out.expr.front().first = 0.0f;
            out.expr.back().first = 1.0f;
            return true;
        }
    };
}

// the Join rules (MpePianoRollModule::joinNotes): false when nothing happens
inline bool joinMpeRollNotes (const MpeRollNote& x, const MpeRollNote& y, bool spline, double grid, MpeRollNote& out)
{
    if (! x.isValid() || ! y.isValid()) return false;
    const MpeRollSegment bridge { spline ? MpeRollSegment::Ease : MpeRollSegment::Line };
    const double eps = 1.0e-6;

    // starting and ending together: nothing to join
    if (std::abs (x.start - y.start) < eps && std::abs (x.end() - y.end()) < eps)
        return false;

    mperoll_detail::Builder b;
    if (std::abs (x.start - y.start) < eps)
    {
        // starting together: the shorter sweeps into the longer over its own length
        const auto& s = x.length() < y.length() ? x : y;
        const auto& l = x.length() < y.length() ? y : x;
        b.addPoint (s.start, s.pitchAt (0.0), {});
        b.ex.push_back ({ s.start, s.exprAt (0.0) });
        b.addPart (l, s.length(), l.length(), false, bridge);
        return b.finish (s.velocity, out);
    }

    const auto& a = x.start < y.start ? x : y;     // the earlier one
    const auto& c = x.start < y.start ? y : x;
    if (a.end() < c.start - eps)
    {
        // apart: the gap bridged
        b.addPart (a, 0.0, a.length(), true);
        b.addPart (c, 0.0, c.length(), false, bridge);
        return b.finish (a.velocity, out);
    }

    // overlapping (or touching): the first cut where the second begins, then a glide
    // into the second over half the overlap (half a grid step when they only touch)
    const double overlap = std::max (a.end() - c.start, 0.5 * grid);
    const double cut = c.start - a.start;
    const double into = std::min (0.5 * overlap, 0.5 * c.length());
    b.addPart (a, 0.0, cut, true);
    b.addPart (c, into, c.length(), false, bridge);
    return b.finish (a.velocity, out);
}

