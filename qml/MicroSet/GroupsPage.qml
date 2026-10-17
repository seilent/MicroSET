pragma ComponentBehavior: Bound
import QtQuick

SettingsPage {
    id: groups

    title: "settings"
    modelRows: groups.model === null ? [] : groups.model.groups(groups.model.revision)
}
