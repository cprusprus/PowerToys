#include "pch.h"
#include "LayoutAssignedWindows.h"

#include <FancyZonesLib/FancyZonesWindowProperties.h>
#include <FancyZonesLib/Settings.h>
#include <FancyZonesLib/VirtualDesktop.h>
#include <FancyZonesLib/WindowUtils.h>

void LayoutAssignedWindows::Assign(HWND window, const ZoneIndexSet& zones)
{
    Dismiss(window);
    
    for (const auto& index : zones)
    {
        m_windowIndexSet[window].push_back(index);
    }

    if (FancyZonesSettings::settings().disableRoundCorners)
    {
        FancyZonesWindowUtils::DisableRoundCorners(window);
    }

    auto tabSortKeyWithinZone = FancyZonesWindowProperties::GetTabSortKeyWithinZone(window);
    InsertWindowIntoZone(window, tabSortKeyWithinZone, zones);
}

void LayoutAssignedWindows::Dismiss(HWND window)
{
    if (m_windowIndexSet.contains(window))
    {
        const auto& indexSet = m_windowIndexSet.at(window);
        auto& windows = m_windowsByIndexSets[indexSet];
        windows.erase(find(begin(windows), end(windows), window));
        if (windows.empty())
        {
            m_windowsByIndexSets.erase(m_windowIndexSet[window]);
        }
        
        m_windowIndexSet.erase(window);
    }
    
    FancyZonesWindowProperties::SetTabSortKeyWithinZone(window, std::nullopt);
}

std::map<HWND, ZoneIndexSet> LayoutAssignedWindows::SnappedWindows() const noexcept
{
    return m_windowIndexSet;
}

ZoneIndexSet LayoutAssignedWindows::GetZoneIndexSetFromWindow(HWND window) const noexcept
{
    auto it = m_windowIndexSet.find(window);
    if (it != m_windowIndexSet.end())
    {
        return it->second;
    }
    
    return {};
}

bool LayoutAssignedWindows::IsZoneEmpty(ZoneIndex zoneIndex) const noexcept
{
    for (auto& [window, zones] : m_windowIndexSet)
    {
        if (find(begin(zones), end(zones), zoneIndex) != end(zones))
        {
            return false;
        }
    }

    return true;
}

void LayoutAssignedWindows::CycleWindows(HWND window, bool reverse)
{
    auto indexSet = GetZoneIndexSetFromWindow(window);

    // Do nothing in case the window is not recognized
    if (indexSet.empty())
    {
        return;
    }

    for (;;)
    {
        auto next = GetNextZoneWindow(indexSet, window, reverse);
        if (!next)
        {
            break;
        }

        // Determine whether the window still exists
        if (!IsWindow(next))
        {
            // Dismiss the encountered window since it was probably closed
            Dismiss(next);
            continue;
        }

        if (VirtualDesktop::instance().IsWindowOnCurrentDesktop(next))
        {
            FancyZonesWindowUtils::SwitchToWindow(next);
        }

        break;
    }
}

std::pair<HWND, int64_t> GetLowestZOrderWindow(const std::vector<HWND>& windows)
{
    // Find which window in windows is at the top of the z-order
    HWND window = GetTopWindow(GetDesktopWindow());
    int64_t zOrder{};
    while (window)
    {
        if (find(begin(windows), end(windows), window) != end(windows))
        {
            return std::make_pair(window, zOrder);
        }

        window = GetWindow(window, GW_HWNDNEXT);
        ++zOrder;
    }

    // TODO: Log not found error
    return std::make_pair(nullptr, 0);
}

HWND LayoutAssignedWindows::GetTopmostWindowFromTargetZone(ZoneIndex targetZone, const ZoneIndexSet& currentWindowZones) noexcept
{
    // Find all ZoneIndexSets which contain target zone, skipping the current window's ZoneIndexSet
    std::vector<ZoneIndexSet> targetZoneIndexSets;
    for (const auto& [indexSet, _] : m_windowsByIndexSets)
    {
        if (indexSet != currentWindowZones)
        {
            if (find(begin(indexSet), end(indexSet), targetZone) != end(indexSet))
            {
                targetZoneIndexSets.emplace_back(indexSet);
            }
        }
    }

    if (targetZoneIndexSets.empty())
    {
        return nullptr;
    }

    // Choose the ZoneIndexSet whose topmost window is topmost accross all index sets
    HWND topmostWindow{};
    int64_t lowestZOrder = INT64_MAX;
    for (const auto& indexSet : targetZoneIndexSets)
    {
        const auto& assignedWindows = m_windowsByIndexSets[indexSet];
        if (assignedWindows.empty())
        {
            return nullptr;
        }

        auto [window, zOrder] = GetLowestZOrderWindow(assignedWindows);
        if (zOrder < lowestZOrder)
        {
            lowestZOrder = zOrder;
            topmostWindow = window;
        }
    }

    return topmostWindow;
}

void LayoutAssignedWindows::InsertWindowIntoZone(HWND window, std::optional<size_t> tabSortKeyWithinZone, const ZoneIndexSet& indexSet)
{
    if (tabSortKeyWithinZone.has_value())
    {
        // Insert the tab using the provided sort key
        auto predicate = [tabSortKeyWithinZone](HWND tab) {
            auto currentTabSortKeyWithinZone = FancyZonesWindowProperties::GetTabSortKeyWithinZone(tab);
            if (currentTabSortKeyWithinZone.has_value())
            {
                return currentTabSortKeyWithinZone.value() > tabSortKeyWithinZone;
            }
            else
            {
                return false;
            }
        };

        auto position = std::find_if(m_windowsByIndexSets[indexSet].begin(), m_windowsByIndexSets[indexSet].end(), predicate);
        m_windowsByIndexSets[indexSet].insert(position, window);
    }
    else
    {
        // Insert the tab at the end
        tabSortKeyWithinZone = 0;
        if (!m_windowsByIndexSets[indexSet].empty())
        {
            auto prevTab = m_windowsByIndexSets[indexSet].back();
            auto prevTabSortKeyWithinZone = FancyZonesWindowProperties::GetTabSortKeyWithinZone(prevTab);
            if (prevTabSortKeyWithinZone.has_value())
            {
                tabSortKeyWithinZone = prevTabSortKeyWithinZone.value() + 1;
            }
        }

        m_windowsByIndexSets[indexSet].push_back(window);
    }

    FancyZonesWindowProperties::SetTabSortKeyWithinZone(window, tabSortKeyWithinZone);
}

HWND LayoutAssignedWindows::GetNextZoneWindow(ZoneIndexSet indexSet, HWND current, bool reverse) noexcept
{
    if (!m_windowsByIndexSets.contains(indexSet))
    {
        return nullptr;
    }

    const auto& assignedWindows = m_windowsByIndexSets[indexSet];
    if (assignedWindows.empty())
    {
        return nullptr;
    }

    auto iter = std::find(assignedWindows.begin(), assignedWindows.end(), current);
    if (!reverse)
    {
        ++iter;
        return iter == assignedWindows.end() ? assignedWindows.front() : *iter;
    }
    else
    {
        return iter == assignedWindows.begin() ? assignedWindows.back() : *(--iter);
    }
}
