external void emit(int v);
#define width 128
#define height 128
uint8_t copy[height, width];
uint8_t hs[3, width];
void sumRow(int j, int s)
{
  for (int i = 1; i < width - 1; i++) {
    hs[s, i] = copy[j, i - 1] + copy[j, i] + copy[j, i + 1];
  }
}
int main()
{
  for (int h = 0; h < width * height; h++) {
    copy[h] = (h * 37 + 11) % 251;
  }
  sumRow(5, 1);
  for (int i = 0; i < width; i++) {
    emit(hs[1, i]);
  }
}
