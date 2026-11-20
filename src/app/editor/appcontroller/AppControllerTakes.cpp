#include "app/editor/appcontroller/AppControllerInternal.h"
#include "app/editor/AppController.h"
#include "app/bin/AssetLibrary.h"

#include <QFileInfo>
#include <QUuid>

using namespace drift::appcontroller_detail;

namespace {

void syncClipToActiveTake(drift::Clip &clip)
{
    if (clip.takes.isEmpty() || clip.activeTakeIndex < 0 || clip.activeTakeIndex >= clip.takes.size())
        return;
    drift::ClipTake &cur = clip.takes[clip.activeTakeIndex];
    cur.assetId = clip.assetId;
    cur.path = clip.path;
    cur.name = clip.name;
    cur.srcIn = clip.srcIn;
    cur.srcOut = clip.srcOut;
    cur.audioStreamIndex = clip.audioStreamIndex;
    cur.thumbnailPath = clip.thumbnailPath;
    cur.filmstripPath = clip.filmstripPath;
    cur.effects = clip.effects;
}

void loadActiveTakeIntoClip(drift::Clip &clip, const drift::Project &project)
{
    if (clip.takes.isEmpty() || clip.activeTakeIndex < 0 || clip.activeTakeIndex >= clip.takes.size())
        return;
    const drift::ClipTake &next = clip.takes.at(clip.activeTakeIndex);
    clip.assetId = next.assetId;
    clip.path = next.path;
    clip.name = next.name;
    clip.srcIn = next.srcIn;
    const drift::TimeUs curSpan = clip.sourceSpanUs();
    if (next.srcOut > next.srcIn) {
        clip.srcOut = next.srcOut;
    } else {
        clip.srcOut = clip.srcIn + curSpan;
    }
    clip.audioStreamIndex = next.audioStreamIndex;
    clip.thumbnailPath = next.thumbnailPath;
    clip.filmstripPath = next.filmstripPath;
    if (!next.effects.isEmpty()) {
        clip.effects = next.effects;
    }
    if (const drift::MediaAsset *a = project.asset(clip.assetId)) {
        if (clip.thumbnailPath.isEmpty())
            clip.thumbnailPath = a->thumbnailPath;
        if (clip.filmstripPath.isEmpty())
            clip.filmstripPath = a->filmstripPath;
        if (a->durationUs > 0 && !clip.loops()) {
            if (clip.srcIn >= a->durationUs)
                clip.srcIn = 0;
            clip.srcOut = qMin(clip.srcIn + curSpan, a->durationUs);
            clip.syncSrcOutFromSpeed(a->durationUs);
        }
    }
}

} // namespace

void AppController::cycleTake(int direction)
{
    QList<QPair<int, int>> pairs = m_selection;
    if (pairs.isEmpty() && isValidClipIndex(m_selectedTrack, m_selectedClip))
        pairs = {qMakePair(m_selectedTrack, m_selectedClip)};
    if (pairs.isEmpty())
        return;
    expandSelectionWithLinkedPartners(m_project, pairs);

    bool hasMultiTakes = false;
    for (const auto &p : std::as_const(pairs)) {
        if (isValidClipIndex(p.first, p.second)) {
            if (m_project.tracks().at(p.first).clips.at(p.second).takes.size() > 1) {
                hasMultiTakes = true;
                break;
            }
        }
    }
    if (!hasMultiTakes)
        return;

    const drift::Project before = m_project.detachedCopy();
    QSet<QString> handledIds;

    for (const auto &p : std::as_const(pairs)) {
        if (!isValidClipIndex(p.first, p.second))
            continue;
        drift::Clip &clip = m_project.tracks()[p.first].clips[p.second];
        if (handledIds.contains(clip.id))
            continue;
        handledIds.insert(clip.id);

        if (clip.takes.size() > 1) {
            syncClipToActiveTake(clip);
            int nextIdx = 0;
            if (direction > 0)
                nextIdx = (clip.activeTakeIndex + 1) % clip.takes.size();
            else
                nextIdx = (clip.activeTakeIndex - 1 + clip.takes.size()) % clip.takes.size();
            clip.activeTakeIndex = nextIdx;
            loadActiveTakeIntoClip(clip, m_project);
        }
    }

    const QString label = direction > 0 ? tr("Next Take") : tr("Previous Take");
    pushProjectEdit(before, label);
    syncClipModels();
    finishEdit(label);
}

void AppController::nextTake()
{
    cycleTake(1);
}

void AppController::previousTake()
{
    cycleTake(-1);
}

void AppController::setActiveTake(int trackIndex, int clipIndex, int takeIndex)
{
    if (!isValidClipIndex(trackIndex, clipIndex))
        return;
    drift::Clip &clip = m_project.tracks()[trackIndex].clips[clipIndex];
    if (takeIndex < 0 || takeIndex >= clip.takes.size() || takeIndex == clip.activeTakeIndex)
        return;

    const drift::Project before = m_project.detachedCopy();
    syncClipToActiveTake(clip);
    clip.activeTakeIndex = takeIndex;
    loadActiveTakeIntoClip(clip, m_project);

    // Synchronize companion clips (e.g. linked audio track)
    if (!clip.linkId.isEmpty()) {
        for (int ti = 0; ti < m_project.tracks().size(); ++ti) {
            for (int ci = 0; ci < m_project.tracks()[ti].clips.size(); ++ci) {
                if (ti == trackIndex && ci == clipIndex)
                    continue;
                drift::Clip &partner = m_project.tracks()[ti].clips[ci];
                if (partner.linkId == clip.linkId && partner.takes.size() == clip.takes.size()
                    && partner.activeTakeIndex != takeIndex) {
                    syncClipToActiveTake(partner);
                    partner.activeTakeIndex = takeIndex;
                    loadActiveTakeIntoClip(partner, m_project);
                }
            }
        }
    }

    const QString label = tr("Set Active Take");
    pushProjectEdit(before, label);
    syncClipModels();
    finishEdit(label);
}

bool AppController::addTakeFromAsset(int trackIndex, int clipIndex, int assetIndex)
{
    if (!m_assetLibrary || !isValidClipIndex(trackIndex, clipIndex))
        return false;
    const QString newAssetId = m_assetLibrary->assetIdAt(assetIndex);
    const drift::MediaAsset *asset = m_project.asset(newAssetId);
    if (!asset)
        return false;

    const drift::Project before = m_project.detachedCopy();
    drift::Clip &clip = m_project.tracks()[trackIndex].clips[clipIndex];

    if (clip.takes.isEmpty()) {
        drift::ClipTake t0;
        t0.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
        t0.name = clip.name.isEmpty() ? QFileInfo(clip.path).fileName() : clip.name;
        t0.assetId = clip.assetId;
        t0.path = clip.path;
        t0.srcIn = clip.srcIn;
        t0.srcOut = clip.srcOut;
        t0.audioStreamIndex = clip.audioStreamIndex;
        t0.thumbnailPath = clip.thumbnailPath;
        t0.filmstripPath = clip.filmstripPath;
        t0.effects = clip.effects;
        clip.takes.append(t0);
        clip.activeTakeIndex = 0;
    } else {
        syncClipToActiveTake(clip);
    }

    drift::ClipTake tNew;
    tNew.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    tNew.name = asset->name.isEmpty() ? QFileInfo(asset->path).fileName() : asset->name;
    tNew.assetId = asset->id;
    tNew.path = asset->path;
    tNew.srcIn = 0;
    tNew.srcOut = clip.sourceSpanUs();
    tNew.audioStreamIndex = 0;
    tNew.thumbnailPath = asset->thumbnailPath;
    tNew.filmstripPath = asset->filmstripPath;
    tNew.effects = clip.effects;
    clip.takes.append(tNew);

    clip.activeTakeIndex = clip.takes.size() - 1;
    loadActiveTakeIntoClip(clip, m_project);

    // Linked partner
    if (!clip.linkId.isEmpty()) {
        for (int ti = 0; ti < m_project.tracks().size(); ++ti) {
            for (int ci = 0; ci < m_project.tracks()[ti].clips.size(); ++ci) {
                if (ti == trackIndex && ci == clipIndex)
                    continue;
                drift::Clip &partner = m_project.tracks()[ti].clips[ci];
                if (partner.linkId == clip.linkId) {
                    if (partner.takes.isEmpty()) {
                        drift::ClipTake pt0;
                        pt0.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
                        pt0.name = partner.name.isEmpty() ? QFileInfo(partner.path).fileName() : partner.name;
                        pt0.assetId = partner.assetId;
                        pt0.path = partner.path;
                        pt0.srcIn = partner.srcIn;
                        pt0.srcOut = partner.srcOut;
                        pt0.audioStreamIndex = partner.audioStreamIndex;
                        pt0.thumbnailPath = partner.thumbnailPath;
                        pt0.filmstripPath = partner.filmstripPath;
                        pt0.effects = partner.effects;
                        partner.takes.append(pt0);
                        partner.activeTakeIndex = 0;
                    } else {
                        syncClipToActiveTake(partner);
                    }
                    drift::ClipTake ptNew;
                    ptNew.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
                    ptNew.name = tNew.name;
                    ptNew.assetId = asset->id;
                    ptNew.path = asset->path;
                    ptNew.srcIn = 0;
                    ptNew.srcOut = partner.sourceSpanUs();
                    ptNew.audioStreamIndex = 0;
                    ptNew.thumbnailPath = asset->thumbnailPath;
                    ptNew.filmstripPath = asset->filmstripPath;
                    ptNew.effects = partner.effects;
                    partner.takes.append(ptNew);
                    partner.activeTakeIndex = partner.takes.size() - 1;
                    loadActiveTakeIntoClip(partner, m_project);
                }
            }
        }
    }

    const QString label = tr("Add Take");
    pushProjectEdit(before, label);
    syncClipModels();
    finishEdit(label);
    return true;
}

bool AppController::deleteActiveTake(int trackIndex, int clipIndex)
{
    if (!isValidClipIndex(trackIndex, clipIndex))
        return false;
    drift::Clip &clip = m_project.tracks()[trackIndex].clips[clipIndex];
    if (clip.takes.size() <= 1)
        return false;

    const drift::Project before = m_project.detachedCopy();
    const int removedIdx = clip.activeTakeIndex;
    clip.takes.removeAt(removedIdx);
    clip.activeTakeIndex = qBound(0, removedIdx, clip.takes.size() - 1);
    loadActiveTakeIntoClip(clip, m_project);

    // Linked partner
    if (!clip.linkId.isEmpty()) {
        for (int ti = 0; ti < m_project.tracks().size(); ++ti) {
            for (int ci = 0; ci < m_project.tracks()[ti].clips.size(); ++ci) {
                if (ti == trackIndex && ci == clipIndex)
                    continue;
                drift::Clip &partner = m_project.tracks()[ti].clips[ci];
                if (partner.linkId == clip.linkId && partner.takes.size() > removedIdx) {
                    partner.takes.removeAt(removedIdx);
                    partner.activeTakeIndex = qBound(0, partner.activeTakeIndex, partner.takes.size() - 1);
                    loadActiveTakeIntoClip(partner, m_project);
                }
            }
        }
    }

    const QString label = tr("Delete Active Take");
    pushProjectEdit(before, label);
    syncClipModels();
    finishEdit(label);
    return true;
}

QVariantList AppController::takesForClip(int trackIndex, int clipIndex) const
{
    QVariantList result;
    if (!isValidClipIndex(trackIndex, clipIndex))
        return result;
    const drift::Clip &clip = m_project.tracks().at(trackIndex).clips.at(clipIndex);
    for (int i = 0; i < clip.takes.size(); ++i) {
        const drift::ClipTake &t = clip.takes.at(i);
        result.append(QVariantMap{
            {QStringLiteral("index"), i},
            {QStringLiteral("id"), t.id},
            {QStringLiteral("name"), t.name},
            {QStringLiteral("assetId"), t.assetId},
            {QStringLiteral("path"), t.path},
            {QStringLiteral("isActive"), i == clip.activeTakeIndex},
        });
    }
    return result;
}
