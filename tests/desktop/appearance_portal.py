"""A settings portal on the private test session bus."""
import dbus
import dbus.service
from dbus.mainloop.glib import DBusGMainLoop
from gi.repository import GLib

class Settings(dbus.service.Object):
    def __init__(self, bus):
        super().__init__(dbus.service.BusName("org.freedesktop.portal.Desktop", bus),
                         "/org/freedesktop/portal/desktop")
        self.values = {"color-scheme": dbus.UInt32(2), "contrast": dbus.UInt32(0),
                       "reduced-motion": dbus.UInt32(0), "accent-color": dbus.Struct((.1,.3,.7), signature="ddd")}
    @dbus.service.method("org.freedesktop.portal.Settings", in_signature="ss", out_signature="v")
    def Read(self, group, key):
        if group == "org.freedesktop.appearance" and key in self.values:
            return self.values[key]
        raise dbus.exceptions.DBusException("Unknown setting", name="org.freedesktop.portal.Error.NotFound")
    @dbus.service.signal("org.freedesktop.portal.Settings", signature="ssv")
    def SettingChanged(self, group, key, value):
        pass
    @dbus.service.method("org.bacuri.TestSettings", in_signature="su")
    def Set(self,key,value):
        self.values[key]=dbus.UInt32(value)
        self.SettingChanged("org.freedesktop.appearance",key,self.values[key])

DBusGMainLoop(set_as_default=True)
settings=Settings(dbus.SessionBus())
GLib.MainLoop().run()
