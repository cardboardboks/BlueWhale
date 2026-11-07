-- /SCRIPTS/WHALE/whale_decode.lua
-- Shared decoder for the custom CRSF frames (protocol v3, 4 ESCs).
-- Used by both /SCRIPTS/TELEMETRY/whale.lua and /WIDGETS/WHALE/main.lua.

local DATA_TYPE   = 0x42
local STATUS_TYPE = 0x43
local VERSION     = 3
local NUM_ESC     = 4

-- Stable IDs so EdgeTX keeps the sensors between sessions. Names max 4 chars.
-- ESC k uses 0x5A<k>1 (temp), 0x5A<k>2 (current), 0x5A<k>3 (voltage)
local ID_AVGV, ID_TOTA, ID_UPT = 0x5A01, 0x5A02, 0x5A03
local UNIT_S = UNIT_SECONDS or UNIT_RAW

local function u16(d, i) return d[i] * 256 + d[i + 1] end
local function s16(d, i) local v = u16(d, i); if v >= 32768 then v = v - 65536 end; return v end
local function u32(d, i) return ((d[i] * 256 + d[i + 1]) * 256 + d[i + 2]) * 256 + d[i + 3] end
local function s32(d, i) local v = u32(d, i); if v >= 2147483648 then v = v - 4294967296 end; return v end

local function readStrings(d, start, count)
  -- EdgeTX's Lua build has no 'table' library, so build strings by concatenation
  local out, n, cur = {}, 0, ""
  for i = start, #d do
    if d[i] == 0 then
      n = n + 1
      out[n] = cur
      cur = ""
      if n == count then break end
    else
      cur = cur .. string.char(d[i])
    end
  end
  return out
end

local function decodeData(t, d)
  if #d < 11 + 6 * NUM_ESC or d[1] ~= VERSION then return end
  t.avgV = u16(d, 2)      -- mV
  t.totA = s32(d, 4)      -- cA
  t.upt  = u32(d, 8)      -- s

  setTelemetryValue(ID_AVGV, 0, 0, math.floor(t.avgV / 10), UNIT_VOLTS, 2, "AvgV")
  setTelemetryValue(ID_TOTA, 0, 0, t.totA, UNIT_AMPS, 2, "TotA")
  setTelemetryValue(ID_UPT,  0, 0, t.upt,  UNIT_S,    0, "Upt")

  for k = 1, NUM_ESC do
    local b = 12 + (k - 1) * 6          -- 1-based index of this ESC's first byte
    local e = t.esc[k]
    e.t, e.i, e.v = s16(d, b), s16(d, b + 2), u16(d, b + 4)
    local id = 0x5A00 + k * 16
    setTelemetryValue(id + 1, 0, 0, e.t, UNIT_CELSIUS, 1, "T" .. k)
    setTelemetryValue(id + 2, 0, 0, e.i, UNIT_AMPS,    2, "I" .. k)
    setTelemetryValue(id + 3, 0, 0, math.floor(e.v / 10), UNIT_VOLTS, 2, "V" .. k)
  end
  t.lastRx = getTime()
end

local function decodeStatus(t, d)
  if #d < 4 or d[1] ~= VERSION then return end
  local s = readStrings(d, 2, 3)
  t.mode, t.boot, t.orient = s[1] or "", s[2] or "", s[3] or ""
  t.lastRx = getTime()
end

local function new()
  local t = { avgV = 0, totA = 0, upt = 0, esc = {},
              mode = "-", boot = "-", orient = "-", lastRx = 0,
              nFrames = 0, lastCmd = 0, lastVer = -1 }
  for k = 1, NUM_ESC do t.esc[k] = { t = 0, i = 0, v = 0 } end
  return t
end

local function poll(t)
  for _ = 1, 10 do
    local cmd, data = crossfireTelemetryPop()
    if cmd == nil then break end
    t.nFrames = t.nFrames + 1     -- debug: every CRSF frame Lua sees
    t.lastCmd = cmd
    if data and (cmd == DATA_TYPE or cmd == STATUS_TYPE) then t.lastVer = data[1] end
    if data then
      if cmd == DATA_TYPE then decodeData(t, data)
      elseif cmd == STATUS_TYPE then decodeStatus(t, data) end
    end
  end
end

local function stale(t) return getTime() - t.lastRx > 150 end   -- 1.5 s

local function fmtUptime(s)
  local fl = math.floor
  return string.format("%d:%02d:%02d", fl(s / 3600), fl(s / 60) % 60, s % 60)
end

-- debug text shown while no data: frames seen, last frame type, version byte of our frames
local function fmtDebug(t)
  return string.format("n%d c%02X v%d", t.nFrames, t.lastCmd, t.lastVer)
end

local function fmtEsc(e)
  return string.format("%.2fV %.2fA %.1fC", e.v / 1000, e.i / 100, e.t / 10)
end

return { new = new, poll = poll, stale = stale, fmtUptime = fmtUptime,
         fmtEsc = fmtEsc, fmtDebug = fmtDebug, NUM_ESC = NUM_ESC }
