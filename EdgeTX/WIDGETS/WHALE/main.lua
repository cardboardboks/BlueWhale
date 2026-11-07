-- /WIDGETS/WHALE/main.lua  (colour radios)
-- Needs /SCRIPTS/WHALE/whale_decode.lua. Best in a large zone or full-screen layout.

local dec

local function create(zone, options)
  dec = dec or loadScript("/SCRIPTS/WHALE/whale_decode.lua")()
  return { zone = zone, options = options, t = dec.new() }
end

local function update(w, options) w.options = options end

local function background(w) dec.poll(w.t) end

local function refresh(w)
  dec.poll(w.t)
  local t, x, y = w.t, w.zone.x + 4, w.zone.y + 2
  local f, h = SMLSIZE, 18

  lcd.drawText(x, y, string.format("%s | %s | %s", t.mode, t.boot, t.orient), BOLD)
  y = y + h + 2
  lcd.drawText(x, y, string.format("Avg %.2f V   Total %.2f A   Up %s",
    t.avgV / 1000, t.totA / 100, dec.fmtUptime(t.upt)), f)

  for k = 1, dec.NUM_ESC do
    y = y + h
    local e = t.esc[k]
    lcd.drawText(x, y, string.format("ESC%d  %.2f V  %.2f A  %.1f C",
      k, e.v / 1000, e.i / 100, e.t / 10), f)
  end
  if dec.stale(t) then lcd.drawText(x, y + h, "No data  " .. dec.fmtDebug(t), f + BLINK) end
end

return { name = "WHALE", options = {}, create = create, update = update,
         background = background, refresh = refresh }
