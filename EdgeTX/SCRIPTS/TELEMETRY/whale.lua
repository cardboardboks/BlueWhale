-- /SCRIPTS/TELEMETRY/whale.lua  (black & white radios, 128x64)
-- Needs /SCRIPTS/WHALE/whale_decode.lua. Select it as a telemetry screen on the model's DISPLAY page.

local dec = loadScript("/SCRIPTS/WHALE/whale_decode.lua")()
local t = dec.new()

local function background() dec.poll(t) end

local function run(event)
  dec.poll(t)
  lcd.clear()
  local f = SMLSIZE

  -- rows are 8 px apart to fit 4 ESC lines on 64 px
  lcd.drawText(0, 0, t.mode, f + INVERS)
  lcd.drawText(60, 0, t.boot, f)
  lcd.drawText(110, 0, t.orient, f)

  lcd.drawText(0, 8, string.format("Avg %.2fV  Tot %.2fA", t.avgV / 1000, t.totA / 100), f)
  lcd.drawText(0, 16, "UpTime " .. dec.fmtUptime(t.upt), f)
  if dec.stale(t) then lcd.drawText(46, 16, "NoData " .. dec.fmtDebug(t), f) end

  for k = 1, dec.NUM_ESC do
    local y = 17 + k * 8           -- 25, 33, 41, 49
    lcd.drawText(0, y, "ESC" .. k, f + INVERS)
    lcd.drawText(24, y, dec.fmtEsc(t.esc[k]), f)
  end
  return 0
end

return { run = run, background = background }
