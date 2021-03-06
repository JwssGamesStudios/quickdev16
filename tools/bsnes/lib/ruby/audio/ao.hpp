/*
  audio.ao (2008-06-01)
  authors: Nach, RedDwarf
*/

class pAudioAO;

class AudioAO : public Audio {
public:
  bool cap(Setting);
  uintptr_t get(Setting);
  bool set(Setting, uintptr_t);

  void sample(uint16_t left, uint16_t right);
  bool init();
  void term();

  AudioAO();
  ~AudioAO();

private:
  pAudioAO &p;
};
