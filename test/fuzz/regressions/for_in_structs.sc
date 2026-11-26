// struct array initializers in C layout ({...} per element, padding, {} as zeros) and for (p in structs): p is the element itself, writes included
external void emit(int v);
struct st { int h; int g; };
struct mix { uint8_t a; float f; uint16_t w; int n; };
st arr[] = {{1, 2}, {3, 4}, {5, -6}};
mix mx[] = {{7, 1.5, 300, -9}, {255, -0.25, 65535, 100000}, {}};
st filled[3];
int p = 77;
void main()
{
   for (p in arr) emit(p.g);
   for (st q : arr) { emit(q.h * 100 + q.g); }
   emit(p);
   for (m in mx)
   {
      emit(m.a);
      emit(m.f * 100);
      emit(m.w);
      emit(m.n);
   }
   for (int i = 0; i < 3; i++) { filled[i].h = i + 1; filled[i].g = (i + 1) * 10; }
   for (e in filled) e.g = e.g + e.h;
   for (e in filled) emit(e.g);
   for (a in arr)
      for (b in arr)
         emit(a.h * 10 + b.h);
}
