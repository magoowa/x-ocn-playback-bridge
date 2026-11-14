-- XOCN-MPC by magoowa, version 1.5.2026.1114 (2026-11-14)
local mp = require 'mp'
local utils = require 'mp.utils'
local ffi = require 'ffi'
ffi.cdef[[int __stdcall SetEnvironmentVariableA(const char*, const char*);]]
local kernel = ffi.load('kernel32')
local root = mp.get_script_directory() or (debug.getinfo(1, 'S').source:sub(2):match('^(.*)[/\\]') or '.')
local tmp = nil
local function quoted(s) return '"' .. s:gsub('"', '') .. '"' end
mp.add_hook('on_load', 50, function()
    kernel.SetEnvironmentVariableA('XOCN_MPV_PREVIEW_4K', nil)
    local path = mp.get_property('stream-open-filename')
    if not path or not path:lower():match('%.mxf$') or path:match('^%a+://') then return end
    local probe = utils.subprocess({args = {utils.join_path(root, 'xocn-mpv-probe.exe'), path}, cancellable = true})
    if probe.status ~= 0 then return end
    local w,h,ax,ay = probe.stdout:match('(%d+)%s+(%d+)%s+(%d+)%s+(%d+)')
    if not w then return end
    if tonumber(w) >= 7680 then kernel.SetEnvironmentVariableA('XOCN_MPV_PREVIEW_4K', '1') end
    local f = os.tmpname() .. '.avs'
    local file = io.open(f, 'wb')
    if not file then mp.msg.error('Cannot create X-OCN bridge script'); return end
    local plugin = utils.join_path(root, 'DirectShowSource.dll')
    file:write('LoadPlugin(', quoted(plugin), ')\nDirectShowSource(', quoted(path), ', pixel_type="RGB24", utf8=true)\n')
    file:close()
    tmp = f
    if tonumber(ax) > 0 and tonumber(ay) > 0 then
        mp.set_property('file-local-options/video-aspect-override', ax .. ':' .. ay)
    end
    mp.set_property('stream-open-filename', f)
    mp.msg.info('Using X-OCN DirectShow bridge')
end)
mp.add_hook('on_unload', 50, function()
    if tmp then os.remove(tmp); tmp = nil end
    kernel.SetEnvironmentVariableA('XOCN_MPV_PREVIEW_4K', nil)
end)
