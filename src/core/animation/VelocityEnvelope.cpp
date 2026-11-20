#include "core/animation/VelocityEnvelope.h"

#include "core/base/Bezier.h"

#include <QtGlobal>

#include <algorithm>
#include <cmath>

namespace drift {

namespace {

constexpr int kSamples = 512;
constexpr double kFlatEpsilon = 1e-9;

} // namespace

QString velocityCurveTypeToString(VelocityCurveType type)
{
    switch (type) {
    case VelocityCurveType::Linear:
        return QStringLiteral("linear");
    case VelocityCurveType::Fast:
        return QStringLiteral("fast");
    case VelocityCurveType::Slow:
        return QStringLiteral("slow");
    case VelocityCurveType::Sharp:
        return QStringLiteral("sharp");
    case VelocityCurveType::Hold:
        return QStringLiteral("hold");
    case VelocityCurveType::Smooth:
    default:
        return QStringLiteral("smooth");
    }
}

VelocityCurveType velocityCurveTypeFromString(const QString &str)
{
    if (str == QLatin1String("linear"))
        return VelocityCurveType::Linear;
    if (str == QLatin1String("fast"))
        return VelocityCurveType::Fast;
    if (str == QLatin1String("slow"))
        return VelocityCurveType::Slow;
    if (str == QLatin1String("sharp"))
        return VelocityCurveType::Sharp;
    if (str == QLatin1String("hold"))
        return VelocityCurveType::Hold;
    return VelocityCurveType::Smooth;
}

void VelocityEnvelope::setPoints(QList<VelocityPoint> points)
{
    if (points.isEmpty()) {
        clear();
        return;
    }

    std::sort(points.begin(), points.end(),
              [](const VelocityPoint &a, const VelocityPoint &b) { return a.pos < b.pos; });

    for (VelocityPoint &point : points) {
        point.pos = qBound(0.0, point.pos, 1.0);
        point.velocity = qBound(kMinVelocity, point.velocity, kMaxVelocity);
    }

    // Ensure boundary points at 0.0 and 1.0 exist
    if (points.first().pos > kFlatEpsilon) {
        VelocityPoint start = points.first();
        start.pos = 0.0;
        points.prepend(start);
    } else {
        points.first().pos = 0.0;
    }

    if (points.last().pos < 1.0 - kFlatEpsilon) {
        VelocityPoint end = points.last();
        end.pos = 1.0;
        points.append(end);
    } else {
        points.last().pos = 1.0;
    }

    // Clamp spline tangent handles to avoid non-monotonic parameterization
    for (int i = 0; i < points.size(); ++i) {
        VelocityPoint &point = points[i];
        const double toPrev = i > 0 ? point.pos - points.at(i - 1).pos : 0.0;
        const double toNext = i + 1 < points.size() ? points.at(i + 1).pos - point.pos : 0.0;
        point.inDx = qBound(-toPrev, point.inDx, 0.0);
        point.outDx = qBound(0.0, point.outDx, toNext);
    }

    m_points = points;
    m_enabled = true;
    rebuild();
}

void VelocityEnvelope::clear()
{
    m_points.clear();
    m_velocities.clear();
    m_cumIntegral.clear();
    m_minIntegral = 0.0;
    m_maxIntegral = 0.0;
    m_enabled = false;
}

void VelocityEnvelope::rebuild()
{
    m_velocities.resize(kSamples + 1);
    m_cumIntegral.resize(kSamples + 1);

    int segment = 0;
    for (int i = 0; i <= kSamples; ++i) {
        const double pos = static_cast<double>(i) / kSamples;
        while (segment + 2 < m_points.size() && m_points.at(segment + 1).pos < pos)
            ++segment;

        const VelocityPoint &a = m_points.at(segment);
        const VelocityPoint &b = m_points.at(segment + 1);
        double v = a.velocity;

        const double span = b.pos - a.pos;
        if (span <= kFlatEpsilon) {
            v = b.velocity;
        } else {
            const double u = qBound(0.0, (pos - a.pos) / span, 1.0);
            switch (a.curveType) {
            case VelocityCurveType::Linear:
                v = a.velocity + (b.velocity - a.velocity) * u;
                break;
            case VelocityCurveType::Hold:
                v = (i == kSamples || pos >= b.pos - kFlatEpsilon) ? b.velocity : a.velocity;
                break;
            case VelocityCurveType::Fast:
                // Fast fade: eases out quickly
                v = a.velocity + (b.velocity - a.velocity) * (1.0 - (1.0 - u) * (1.0 - u));
                break;
            case VelocityCurveType::Slow:
                // Slow fade: starts slow, accelerates
                v = a.velocity + (b.velocity - a.velocity) * (u * u);
                break;
            case VelocityCurveType::Sharp: {
                // Sharp fade: fast middle transition
                const double s = u < 0.5 ? 4.0 * u * u * u : 1.0 - 4.0 * (1.0 - u) * (1.0 - u) * (1.0 - u);
                v = a.velocity + (b.velocity - a.velocity) * s;
                break;
            }
            case VelocityCurveType::Smooth:
            default: {
                const bool hasHandles = !qFuzzyIsNull(a.outDx) || !qFuzzyIsNull(a.outDy)
                                     || !qFuzzyIsNull(b.inDx) || !qFuzzyIsNull(b.inDy);
                if (hasHandles) {
                    const double t = bezierParameterForX(a.pos, a.pos + a.outDx, b.pos + b.inDx, b.pos, pos);
                    v = cubicBezier(a.velocity, a.velocity + a.outDy, b.velocity + b.inDy, b.velocity, t);
                } else {
                    // Standard smooth cubic Hermite / smoothstep
                    const double s = 3.0 * u * u - 2.0 * u * u * u;
                    v = a.velocity + (b.velocity - a.velocity) * s;
                }
                break;
            }
            }
        }

        m_velocities[i] = qBound(kMinVelocity, v, kMaxVelocity);
    }

    // Cumulative integral calculation using trapezoidal integration
    const double dp = 1.0 / kSamples;
    m_cumIntegral[0] = 0.0;
    m_minIntegral = 0.0;
    m_maxIntegral = 0.0;

    for (int i = 0; i < kSamples; ++i) {
        const double v0 = m_velocities.at(i);
        const double v1 = m_velocities.at(i + 1);
        const double dInt = 0.5 * (v0 + v1) * dp;
        const double nextVal = m_cumIntegral.at(i) + dInt;
        m_cumIntegral[i + 1] = nextVal;
        if (nextVal < m_minIntegral)
            m_minIntegral = nextVal;
        if (nextVal > m_maxIntegral)
            m_maxIntegral = nextVal;
    }
}

double VelocityEnvelope::velocityAt(double pos) const
{
    if (isEmpty())
        return kDefaultVelocity;

    const double x = qBound(0.0, pos, 1.0) * kSamples;
    const int i = qMin(static_cast<int>(x), kSamples - 1);
    const double f = x - static_cast<double>(i);
    return m_velocities.at(i) + (m_velocities.at(i + 1) - m_velocities.at(i)) * f;
}

double VelocityEnvelope::velocityAtTimelineOffset(TimeUs timelineOffsetUs, TimeUs timelineDurationUs) const
{
    if (isEmpty() || timelineDurationUs <= 0)
        return kDefaultVelocity;

    const double pos = qBound(0.0, static_cast<double>(timelineOffsetUs) / static_cast<double>(timelineDurationUs), 1.0);
    return velocityAt(pos);
}

TimeUs VelocityEnvelope::sourceTimeForTimelineOffset(TimeUs timelineOffsetUs, TimeUs timelineDurationUs,
                                                     TimeUs srcIn, TimeUs srcOut, bool reverse) const
{
    if (isEmpty() || !m_enabled || timelineDurationUs <= 0) {
        if (reverse) {
            const TimeUs range = srcOut - srcIn;
            return (range > 0) ? srcOut - qMin(timelineOffsetUs, range) : srcIn;
        }
        return srcIn + timelineOffsetUs;
    }

    const double clampedRel = qBound(0.0, static_cast<double>(timelineOffsetUs), static_cast<double>(timelineDurationUs));
    const double pos = clampedRel / static_cast<double>(timelineDurationUs);

    const double x = pos * kSamples;
    const int i = qMin(static_cast<int>(x), kSamples - 1);
    const double frac = x - static_cast<double>(i);

    const double intNorm = m_cumIntegral.at(i) + (m_cumIntegral.at(i + 1) - m_cumIntegral.at(i)) * frac;
    const double dur = static_cast<double>(timelineDurationUs);
    const double integralUs = intNorm * dur;

    const TimeUs range = srcOut - srcIn;
    const double minIntegralUs = m_minIntegral * dur;

    // Offset the starting point when velocity causes displacement to travel backward before start
    const double startOffsetUs = (minIntegralUs < 0.0)
        ? qMin(static_cast<double>(range), -minIntegralUs)
        : 0.0;

    TimeUs sourceUs = srcIn + static_cast<TimeUs>(std::llround(startOffsetUs + integralUs));

    if (reverse) {
        if (range > 0)
            sourceUs = srcOut - (sourceUs - srcIn);
    }

    const TimeUs minBound = qMin(srcIn, srcOut);
    const TimeUs maxBound = qMax(srcIn, srcOut);
    return qBound(minBound, sourceUs, maxBound);
}

TimeUs VelocityEnvelope::timelineOffsetForSourceTime(TimeUs sourceUs, TimeUs timelineDurationUs,
                                                     TimeUs srcIn, TimeUs srcOut, bool reverse) const
{
    if (isEmpty() || !m_enabled || timelineDurationUs <= 0)
        return 0;

    TimeUs bestTimelineUs = 0;
    TimeUs minDiff = -1;

    for (int i = 0; i <= kSamples; ++i) {
        const double pos = static_cast<double>(i) / kSamples;
        const TimeUs tUs = static_cast<TimeUs>(std::llround(pos * static_cast<double>(timelineDurationUs)));
        const TimeUs sUs = sourceTimeForTimelineOffset(tUs, timelineDurationUs, srcIn, srcOut, reverse);
        const TimeUs diff = qAbs(sUs - sourceUs);
        if (minDiff < 0 || diff < minDiff) {
            minDiff = diff;
            bestTimelineUs = tUs;
            if (diff == 0)
                break;
        }
    }
    return bestTimelineUs;
}

VelocityEnvelope VelocityEnvelope::subRange(double p0, double p1) const
{
    VelocityEnvelope out;
    if (isEmpty() || p1 - p0 <= kFlatEpsilon)
        return out;

    constexpr int kResampled = 33;
    QList<VelocityPoint> pts;
    pts.reserve(kResampled);
    for (int i = 0; i < kResampled; ++i) {
        const double f = static_cast<double>(i) / (kResampled - 1);
        const double pos = p0 + (p1 - p0) * f;
        VelocityPoint pt;
        pt.pos = f;
        pt.velocity = velocityAt(pos);
        pt.curveType = VelocityCurveType::Smooth;
        pts.append(pt);
    }
    out.setPoints(pts);
    out.setEnabled(m_enabled);
    return out;
}

QJsonObject VelocityEnvelope::toJson() const
{
    QJsonObject obj;
    obj.insert(QStringLiteral("enabled"), m_enabled);
    QJsonArray pts;
    for (const VelocityPoint &point : m_points) {
        pts.append(QJsonObject{
            {QStringLiteral("pos"), point.pos},
            {QStringLiteral("velocity"), point.velocity},
            {QStringLiteral("curveType"), velocityCurveTypeToString(point.curveType)},
            {QStringLiteral("inDx"), point.inDx},
            {QStringLiteral("inDy"), point.inDy},
            {QStringLiteral("outDx"), point.outDx},
            {QStringLiteral("outDy"), point.outDy},
        });
    }
    obj.insert(QStringLiteral("points"), pts);
    return obj;
}

VelocityEnvelope VelocityEnvelope::fromJson(const QJsonObject &obj)
{
    VelocityEnvelope env;
    env.m_enabled = obj.value(QStringLiteral("enabled")).toBool(true);

    const QJsonArray ptsArray = obj.value(QStringLiteral("points")).toArray();
    QList<VelocityPoint> pts;
    pts.reserve(ptsArray.size());
    for (const QJsonValue &v : ptsArray) {
        const QJsonObject pObj = v.toObject();
        VelocityPoint pt;
        pt.pos = pObj.value(QStringLiteral("pos")).toDouble(0.0);
        pt.velocity = pObj.value(QStringLiteral("velocity")).toDouble(1.0);
        pt.curveType = velocityCurveTypeFromString(pObj.value(QStringLiteral("curveType")).toString());
        pt.inDx = pObj.value(QStringLiteral("inDx")).toDouble(0.0);
        pt.inDy = pObj.value(QStringLiteral("inDy")).toDouble(0.0);
        pt.outDx = pObj.value(QStringLiteral("outDx")).toDouble(0.0);
        pt.outDy = pObj.value(QStringLiteral("outDy")).toDouble(0.0);
        pts.append(pt);
    }

    if (!pts.isEmpty()) {
        env.setPoints(pts);
        env.m_enabled = obj.value(QStringLiteral("enabled")).toBool(true);
    }
    return env;
}

VelocityEnvelope VelocityEnvelope::createDefault()
{
    VelocityPoint start;
    start.pos = 0.0;
    start.velocity = kDefaultVelocity;
    start.curveType = VelocityCurveType::Smooth;

    VelocityPoint end;
    end.pos = 1.0;
    end.velocity = kDefaultVelocity;
    end.curveType = VelocityCurveType::Smooth;

    VelocityEnvelope env;
    env.setPoints({start, end});
    env.setEnabled(true);
    return env;
}

} // namespace drift
