#pragma once

#include "core/base/Time.h"

#include <QJsonArray>
#include <QJsonObject>
#include <QList>
#include <QString>
#include <QVector>

namespace drift {

// VEGAS Pro velocity envelope limits: -100% (reverse) to +1000% (10x fast forward).
inline constexpr double kMinVelocity = -1.0;
inline constexpr double kMaxVelocity = 10.0;
inline constexpr double kDefaultVelocity = 1.0;
inline constexpr double kFreezeVelocity = 0.0;

enum class VelocityCurveType
{
    Linear,
    Fast,
    Slow,
    Smooth, // Spline with continuous slope
    Sharp,
    Hold   // Step: freeze velocity until next point
};

QString velocityCurveTypeToString(VelocityCurveType type);
VelocityCurveType velocityCurveTypeFromString(const QString &str);

// One control point of a velocity envelope along a clip's timeline duration.
// `pos` is normalized in [0.0, 1.0] representing position from clip start to clip end.
// `velocity` is in [-1.0, 10.0], where 1.0 = 100% (normal forward), 0.0 = freeze frame,
// -1.0 = -100% (reverse playback), up to 10.0 = 1000% (10x speed).
struct VelocityPoint
{
    double pos = 0.0;
    double velocity = 1.0;
    VelocityCurveType curveType = VelocityCurveType::Smooth;
    double inDx = 0.0;
    double inDy = 0.0;
    double outDx = 0.0;
    double outDy = 0.0;

    bool operator==(const VelocityPoint &other) const
    {
        return qFuzzyCompare(pos + 1.0, other.pos + 1.0)
            && qFuzzyCompare(velocity + 1.0, other.velocity + 1.0)
            && curveType == other.curveType
            && qFuzzyCompare(inDx + 1.0, other.inDx + 1.0)
            && qFuzzyCompare(inDy + 1.0, other.inDy + 1.0)
            && qFuzzyCompare(outDx + 1.0, other.outDx + 1.0)
            && qFuzzyCompare(outDy + 1.0, other.outDy + 1.0);
    }
};

// Sony VEGAS Pro style Velocity Envelope for video clips.
//
// Defines playback rate as a function of timeline time across the clip.
// The integral of velocity yields the source displacement:
//   * velocity = 1.0: advances source at normal speed
//   * velocity = 0.0: source displacement is zero (freeze-frame)
//   * velocity = -1.0: source displacement runs backwards (reverse playback)
//   * velocity = 10.0: advances source 10x faster (high-speed fast forward)
class VelocityEnvelope
{
public:
    VelocityEnvelope() = default;

    bool isEmpty() const { return m_points.isEmpty(); }
    bool enabled() const { return m_enabled && !m_points.isEmpty(); }
    void setEnabled(bool enabled) { m_enabled = enabled; }

    const QList<VelocityPoint> &points() const { return m_points; }
    void setPoints(QList<VelocityPoint> points);
    void clear();

    // Instantaneous velocity at normalized pos in [0, 1].
    double velocityAt(double pos) const;

    // Instantaneous velocity at timeline offset in microseconds.
    double velocityAtTimelineOffset(TimeUs timelineOffsetUs, TimeUs timelineDurationUs) const;

    // Map timeline offset to source time in microseconds within the media range.
    TimeUs sourceTimeForTimelineOffset(TimeUs timelineOffsetUs, TimeUs timelineDurationUs,
                                       TimeUs srcIn, TimeUs srcOut, bool reverse = false) const;

    // Inverse mapping: find closest timeline offset for a source time.
    TimeUs timelineOffsetForSourceTime(TimeUs sourceUs, TimeUs timelineDurationUs,
                                       TimeUs srcIn, TimeUs srcOut, bool reverse = false) const;

    // Resampled sub-range for clip split operations.
    VelocityEnvelope subRange(double p0, double p1) const;

    QJsonObject toJson() const;
    static VelocityEnvelope fromJson(const QJsonObject &obj);

    // Initial flat envelope at 100% velocity (normal forward).
    static VelocityEnvelope createDefault();

    void detachSharedData()
    {
        m_points.detach();
        m_velocities.detach();
        m_cumIntegral.detach();
    }

private:
    void rebuild();

    bool m_enabled = false;
    QList<VelocityPoint> m_points;

    QVector<double> m_velocities;   // velocity at pos i / kSamples
    QVector<double> m_cumIntegral;  // cumulative integral of velocity * dt
    double m_minIntegral = 0.0;
    double m_maxIntegral = 0.0;
};

} // namespace drift
