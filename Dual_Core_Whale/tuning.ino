
void loadTune() {
  float tmp[NUM_TUNE];
  prefs.begin("whale", true);  // read-only
  size_t n = prefs.getBytesLength("tune");
  if (n == sizeof(tmp)) {  // only trust it if the size matches (changing NUM_TUNE falls back to defaults)
    prefs.getBytes("tune", tmp, sizeof(tmp));
    for (int i = 0; i < NUM_TUNE; i++) tuneVal[i] = tmp[i];
  }
  for (int i = 0; i < NUM_TUNE_STR; i++) {
    char key[8], txt[TUNE_STR_LEN] = "";
    snprintf(key, sizeof(key), "ts%d", i);
    prefs.getString(key, txt, sizeof(txt));  // stays empty if nothing was saved yet
    strlcpy(tuneStr[i], txt, TUNE_STR_LEN);
  }
  prefs.end();
}

void saveTune() {
  float tmp[NUM_TUNE];
  for (int i = 0; i < NUM_TUNE; i++) tmp[i] = tuneVal[i];
  prefs.begin("whale", false);  // read/write
  prefs.putBytes("tune", tmp, sizeof(tmp));
  for (int i = 0; i < NUM_TUNE_STR; i++) {
    char key[8], txt[TUNE_STR_LEN];
    snprintf(key, sizeof(key), "ts%d", i);
    strlcpy(txt, tuneStr[i], TUNE_STR_LEN);
    prefs.putString(key, txt);
  }
  prefs.end();
}