external void emit(int v);
#define width 128
#define height 128
uint8_t copy[height, width];
uint8_t hs[3, width];
void ruleRow(int j, int a, int c, int b)
{
  for (int i = 1; i < width - 1; i++) {
    uint8_t n = hs[a, i] + hs[c, i] + hs[b, i];
    copy[j, i] = (n == 3) | ((n == 4) & copy[j, i]);
  }
}
int main()
{
  for (int h = 0; h < width * height; h++) {
    copy[h] = (h * 37 + 11) % 3;
  }
  for (int h = 0; h < 3 * width; h++) {
    hs[h] = (h * 53 + 7) % 4;
  }
  ruleRow(5, 0, 1, 2);
  for (int i = 0; i < width; i++) {
    emit(copy[5, i]);
  }
}
