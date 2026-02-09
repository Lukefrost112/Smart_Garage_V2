#include <Servo.h>

#define IR1 1      // حساس خارجي
#define IR2 2       // حساس داخلي
#define MOTOR_PIN 5

Servo gate;

// --- متغيرات الحالة (State Variables) ---
int systemState = 0;      // 0 = سكون، 1 = دخول، 2 = خروج
bool reachedEnd = false;  // هل وصلت السيارة للحساس الثاني؟

// --- متغيرات الحركة ---
int currentAngle = 0;
int targetAngle = 0;
unsigned long lastMoveTime = 0;

void setup() {
  pinMode(IR1, INPUT);
  pinMode(IR2, INPUT);
  gate.attach(MOTOR_PIN);
  gate.write(0);
}

void loop() {
  unsigned long now = millis();
  
  // قراءة الحساسات (LOW يعني يوجد سيارة)
  bool s1 = (digitalRead(IR1) == LOW);
  bool s2 = (digitalRead(IR2) == LOW);

  // ---------------------------------------------------------
  // الجزء الأول: تحديد حالة النظام (المنطق)
  // ---------------------------------------------------------

  // الحالة 0: السكون (انتظار سيارة)
  if (systemState == 0) {
    targetAngle = 0; // البوابة مغلقة
    
    if (s1) { // سيارة قادمة من الخارج (دخول)
      systemState = 1;
      reachedEnd = false;
    }
    else if (s2) { // سيارة قادمة من الداخل (خروج)
      systemState = 2;
      reachedEnd = false;
    }
  }

  // الحالة 1: عملية الدخول (IR1 -> IR2)
  else if (systemState == 1) {
    targetAngle = 90; // افتح البوابة
    
    // هل وصلت السيارة للحساس الداخلي؟
    if (s2) {
      reachedEnd = true; 
    }
    
    // شرط الإغلاق: وصلت للنهاية + الطريق أصبح فارغاً تماماً
    if (reachedEnd == true && !s1 && !s2) {
      systemState = 0; // عودة للسكون
    }
  }

  // الحالة 2: عملية الخروج (IR2 -> IR1)
  else if (systemState == 2) {
    targetAngle = 90; // افتح البوابة
    
    // هل وصلت السيارة للحساس الخارجي؟
    if (s1) {
      reachedEnd = true;
    }
    
    // شرط الإغلاق: وصلت للنهاية + الطريق أصبح فارغاً تماماً
    if (reachedEnd == true && !s1 && !s2) {
      systemState = 0; // عودة للسكون
    }
  }

  // ---------------------------------------------------------
  // الجزء الثاني: تنفيذ الحركة الناعمة
  // ---------------------------------------------------------
  if (now - lastMoveTime >= 15) {
    lastMoveTime = now;
    if (currentAngle < targetAngle) {
      currentAngle++;
      gate.write(currentAngle);
    }
    else if (currentAngle > targetAngle) {
      currentAngle--;
      gate.write(currentAngle);
    }
  }
}