/* USER CODE BEGIN Header */
/* USER CODE END Header */

/* Includes ------------------------------------------------------------------*/
#include "main.h"

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */
#define BUZZER_PORT GPIOA
#define BUZZER_PIN  GPIO_PIN_12
/* USER CODE END PD */

/* Private variables ---------------------------------------------------------*/
TIM_HandleTypeDef htim3;

/* USER CODE BEGIN PV */
int current_floor = 1;           // 현재 층 (초기값: 1층)
int target_floor = -1;           // 목표 층 (-1: 목표 없음)
int floor_requests[5] = {0, 0, 0, 0, 0};  // 각 층 호출 요청 배열 (1~5층)
int direction = 0;               // 이동 방향 (1: 상승, -1: 하강, 0: 정지)
int door_opened = 0;             // 도어 상태 (1: 열림, 0: 닫힘)
/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
void SystemClock_Config(void);
static void MX_GPIO_Init(void);
static void MX_TIM3_Init(void);

/* USER CODE BEGIN 0 */

/**
 * @brief 4x4 키패드를 스캔하여 눌린 키를 반환합니다.
 * @return 눌린 키의 문자값, 없으면 '\0'
 */
char Keypad_Scan(void)
{
    char keymap[4][4] = {
        {'1', '2', '3', 'A'},
        {'4', '5', '6', 'B'},
        {'7', '8', '9', 'C'},
        {'*', '0', '#', 'D'}
    };

    // Row 핀 (출력)
    GPIO_TypeDef* RowPorts[4] = {GPIOC, GPIOC, GPIOA, GPIOB};
    uint16_t RowPins[4] = {GPIO_PIN_5, GPIO_PIN_4, GPIO_PIN_10, GPIO_PIN_3};

    // Col 핀 (입력)
    GPIO_TypeDef* ColPorts[4] = {GPIOB, GPIOB, GPIOB, GPIOA};
    uint16_t ColPins[4] = {GPIO_PIN_5, GPIO_PIN_4, GPIO_PIN_10, GPIO_PIN_8};

    for (int row = 0; row < 4; row++) {
        // 모든 Row를 HIGH로 설정
        for (int i = 0; i < 4; i++) {
            HAL_GPIO_WritePin(RowPorts[i], RowPins[i], GPIO_PIN_SET);
        }
        // 현재 Row만 LOW로 설정하여 해당 행 활성화
        HAL_GPIO_WritePin(RowPorts[row], RowPins[row], GPIO_PIN_RESET);
        HAL_Delay(1);  // 신호 안정화 대기

        // 각 Col 입력 확인
        for (int col = 0; col < 4; col++) {
            if (HAL_GPIO_ReadPin(ColPorts[col], ColPins[col]) == GPIO_PIN_RESET) {
                // 키 떼질 때까지 대기 (디바운스)
                while (HAL_GPIO_ReadPin(ColPorts[col], ColPins[col]) == GPIO_PIN_RESET);
                return keymap[row][col];
            }
        }
    }
    return '\0';  // 눌린 키 없음
}

/* 7-세그먼트 핀 배열 (A~DP 순서) */
GPIO_TypeDef* SEG_PORT[8] = {GPIOA, GPIOA, GPIOB, GPIOA, GPIOA, GPIOA, GPIOB, GPIOB};
uint16_t SEG_PIN[8] = {GPIO_PIN_4, GPIO_PIN_0, GPIO_PIN_9, GPIO_PIN_6,
                        GPIO_PIN_7, GPIO_PIN_1, GPIO_PIN_8, GPIO_PIN_6};

/* 숫자별 세그먼트 패턴 (공통 애노드 기준, 0 = ON) */
const uint8_t SEGMENT_PATTERN[10][8] = {
    {0, 0, 0, 0, 0, 0, 1, 1}, // '0'
    {1, 0, 0, 1, 1, 1, 1, 1}, // '1'
    {0, 0, 1, 0, 0, 1, 0, 1}, // '2'
    {0, 0, 0, 0, 1, 1, 0, 1}, // '3'
    {1, 0, 0, 1, 1, 0, 0, 1}, // '4'
    {0, 1, 0, 0, 1, 0, 0, 1}, // '5'
    {0, 1, 0, 0, 0, 0, 0, 1}, // '6'
    {0, 0, 0, 1, 1, 1, 1, 1}, // '7'
    {0, 0, 0, 0, 0, 0, 0, 1}, // '8'
    {0, 0, 0, 0, 1, 0, 0, 1}  // '9'
};

/**
 * @brief 7-세그먼트에 숫자를 표시합니다.
 * @param num 표시할 문자 ('0'~'9')
 */
void display_digit(char num)
{
    if (num < '0' || num > '9') return;

    uint8_t index = num - '0';
    for (int i = 0; i < 8; i++) {
        HAL_GPIO_WritePin(SEG_PORT[i], SEG_PIN[i],
                          SEGMENT_PATTERN[index][i] ? GPIO_PIN_SET : GPIO_PIN_RESET);
    }
}

/**
 * @brief 특정 층의 호출 요청을 등록합니다.
 * @param floor 호출할 층 (1~5)
 */
void add_floor_request(int floor) {
    if (floor < 1 || floor > 5) return;
    floor_requests[floor - 1] = 1;
}

/**
 * @brief 다음 목표 층을 반환합니다.
 * @return 호출 요청이 있는 가장 낮은 층 번호, 없으면 -1
 */
int find_next_target() {
    for (int i = 0; i < 5; i++) {
        if (floor_requests[i]) {
            return i + 1;
        }
    }
    return -1;
}

/* 홀 센서 핀 배열 (1~5층 순서) */
GPIO_TypeDef* HALL_PORTS[5] = {GPIOC, GPIOC, GPIOA, GPIOC, GPIOC};
uint16_t HALL_PINS[5] = {GPIO_PIN_1, GPIO_PIN_0, GPIO_PIN_9, GPIO_PIN_6, GPIO_PIN_10};

/**
 * @brief 홀 센서를 통해 현재 층을 감지합니다.
 * @return 감지된 층 번호 (1~5), 감지되지 않으면 -1
 */
int detect_current_floor() {
    for (int i = 0; i < 5; i++) {
        if (HAL_GPIO_ReadPin(HALL_PORTS[i], HALL_PINS[i]) == GPIO_PIN_RESET) {
            return i + 1;
        }
    }
    return -1;  // 층 감지 실패
}

/* 모터 제어 함수 (PWM 듀티비로 방향 제어) */
void motor_up()   { __HAL_TIM_SET_COMPARE(&htim3, TIM_CHANNEL_2, 2100); } // 상승
void motor_down() { __HAL_TIM_SET_COMPARE(&htim3, TIM_CHANNEL_2, 900);  } // 하강
void motor_stop() { __HAL_TIM_SET_COMPARE(&htim3, TIM_CHANNEL_2, 1500); } // 정지

/* 부저 제어 함수 */
void buzzer_on()  { HAL_GPIO_WritePin(BUZZER_PORT, BUZZER_PIN, GPIO_PIN_SET);   }
void buzzer_off() { HAL_GPIO_WritePin(BUZZER_PORT, BUZZER_PIN, GPIO_PIN_RESET); }

/**
 * @brief 도어를 열고 부저 알림 후 3초 대기, 이후 자동으로 닫습니다.
 *        대기 중 '*' 키 입력 시 즉시 닫힙니다.
 */
void open_door_with_buzzer() {
    if (door_opened) return;  // 이미 열려있으면 무시

    door_opened = 1;

    // 열림 알림음
    buzzer_on();
    HAL_Delay(300);
    buzzer_off();

    // 3초 대기 (도중 '*' 입력 시 강제 닫힘)
    uint32_t start = HAL_GetTick();
    while (HAL_GetTick() - start < 3000) {
        char k = Keypad_Scan();
        if (k == '*') break;
        HAL_Delay(50);
    }

    // 닫힘 전 경고음 (3회)
    for (int i = 0; i < 3; i++) {
        buzzer_on();
        HAL_Delay(100);
        buzzer_off();
        HAL_Delay(100);
    }

    door_opened = 0;
}

/**
 * @brief 도어를 수동으로 닫습니다. 경고음 후 닫힘 처리합니다.
 */
void close_door_with_buzzer() {
    if (!door_opened) return;  // 이미 닫혀있으면 무시

    // 닫힘 전 경고음 (3회)
    for (int i = 0; i < 3; i++) {
        buzzer_on();
        HAL_Delay(100);
        buzzer_off();
        HAL_Delay(100);
    }

    door_opened = 0;
}

/* USER CODE END 0 */

/**
 * @brief 메인 함수 - 엘리베이터 제어 메인 루프
 */
int main(void)
{
    HAL_Init();
    SystemClock_Config();
    MX_GPIO_Init();
    MX_TIM3_Init();

    HAL_TIM_PWM_Start(&htim3, TIM_CHANNEL_2);  // PWM 출력 시작

    while (1)
    {
        char key = Keypad_Scan();

        // 현재 층 세그먼트 표시
        if (key >= '0' && key <= '9') {
            display_digit(key);
        }

        // 키패드 입력에 따른 층 호출 등록
        if (key >= '1' && key <= '5') {
            add_floor_request(key - '0');         // 외부 호출: 1~5층
        } else if (key >= '6' && key <= '9') {
            add_floor_request(key - '6' + 1);     // 내부 호출: 6~9 → 1~4층
        } else if (key == '0') {
            add_floor_request(5);                  // 내부 호출: 0 → 5층
        } else if (key == '#') {
            open_door_with_buzzer();               // 수동 문 열기
        } else if (key == '*') {
            close_door_with_buzzer();              // 수동 문 닫기
        }

        // 목표 층 설정 (호출 요청이 있을 경우)
        if (target_floor == -1) {
            target_floor = find_next_target();
            if (target_floor > current_floor)       direction = 1;
            else if (target_floor < current_floor)  direction = -1;
            else                                    direction = 0;
        }

        // 엘리베이터 이동 처리
        if (target_floor != -1 && current_floor != target_floor) {
            // 방향에 따라 모터 구동
            if (target_floor > current_floor) motor_up();
            else                              motor_down();

            int last_floor = current_floor;

            while (1) {
                // 이동 중에도 키패드 입력 수신
                char key = Keypad_Scan();
                if (key >= '1' && key <= '5')       add_floor_request(key - '0');
                else if (key >= '6' && key <= '9')  add_floor_request(key - '6' + 1);
                else if (key == '0')                add_floor_request(5);

                // 홀 센서로 현재 층 감지
                int detected = detect_current_floor();

                // 도어가 닫힌 상태에서만 층 변경 처리 (인터락)
                if (detected != -1 && detected != last_floor && door_opened == 0) {
                    current_floor = detected;
                    display_digit(current_floor + '0');
                    last_floor = detected;

                    // 목표 층 도달 시 정지 및 도어 개방
                    if (current_floor == target_floor) {
                        motor_stop();
                        floor_requests[target_floor - 1] = 0;  // 호출 요청 해제
                        target_floor = -1;
                        direction = 0;
                        open_door_with_buzzer();
                        HAL_Delay(500);  // 기계적 관성 안정화 대기
                        break;
                    }
                }

                HAL_Delay(10);  // 키패드 폴링 간격
            }
        }
    }
}

void SystemClock_Config(void)
{
    RCC_OscInitTypeDef RCC_OscInitStruct = {0};
    RCC_ClkInitTypeDef RCC_ClkInitStruct = {0};

    HAL_PWREx_ControlVoltageScaling(PWR_REGULATOR_VOLTAGE_SCALE1_BOOST);

    RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSI;
    RCC_OscInitStruct.HSIState = RCC_HSI_ON;
    RCC_OscInitStruct.HSICalibrationValue = RCC_HSICALIBRATION_DEFAULT;
    RCC_OscInitStruct.PLL.PLLState = RCC_PLL_ON;
    RCC_OscInitStruct.PLL.PLLSource = RCC_PLLSOURCE_HSI;
    RCC_OscInitStruct.PLL.PLLM = RCC_PLLM_DIV4;
    RCC_OscInitStruct.PLL.PLLN = 85;
    RCC_OscInitStruct.PLL.PLLP = RCC_PLLP_DIV2;
    RCC_OscInitStruct.PLL.PLLQ = RCC_PLLQ_DIV2;
    RCC_OscInitStruct.PLL.PLLR = RCC_PLLR_DIV2;
    if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK) Error_Handler();

    RCC_ClkInitStruct.ClockType = RCC_CLOCKTYPE_HCLK | RCC_CLOCKTYPE_SYSCLK
                                | RCC_CLOCKTYPE_PCLK1 | RCC_CLOCKTYPE_PCLK2;
    RCC_ClkInitStruct.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK;
    RCC_ClkInitStruct.AHBCLKDivider = RCC_SYSCLK_DIV1;
    RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV1;
    RCC_ClkInitStruct.APB2CLKDivider = RCC_HCLK_DIV1;
    if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_4) != HAL_OK) Error_Handler();
}

static void MX_TIM3_Init(void)
{
    TIM_MasterConfigTypeDef sMasterConfig = {0};
    TIM_OC_InitTypeDef sConfigOC = {0};

    // TIM3: 20ms 주기 PWM (서보 모터 제어용)
    htim3.Instance = TIM3;
    htim3.Init.Prescaler = 169;
    htim3.Init.CounterMode = TIM_COUNTERMODE_UP;
    htim3.Init.Period = 19999;
    htim3.Init.ClockDivision = TIM_CLOCKDIVISION_DIV1;
    htim3.Init.AutoReloadPreload = TIM_AUTORELOAD_PRELOAD_DISABLE;
    if (HAL_TIM_PWM_Init(&htim3) != HAL_OK) Error_Handler();

    sMasterConfig.MasterOutputTrigger = TIM_TRGO_RESET;
    sMasterConfig.MasterSlaveMode = TIM_MASTERSLAVEMODE_DISABLE;
    if (HAL_TIMEx_MasterConfigSynchronization(&htim3, &sMasterConfig) != HAL_OK) Error_Handler();

    // PWM 채널 2 설정 (초기 펄스: 1500 = 정지)
    sConfigOC.OCMode = TIM_OCMODE_PWM1;
    sConfigOC.Pulse = 1500;
    sConfigOC.OCPolarity = TIM_OCPOLARITY_HIGH;
    sConfigOC.OCFastMode = TIM_OCFAST_DISABLE;
    if (HAL_TIM_PWM_ConfigChannel(&htim3, &sConfigOC, TIM_CHANNEL_2) != HAL_OK) Error_Handler();

    HAL_TIM_MspPostInit(&htim3);
}

static void MX_GPIO_Init(void)
{
    GPIO_InitTypeDef GPIO_InitStruct = {0};

    __HAL_RCC_GPIOC_CLK_ENABLE();
    __HAL_RCC_GPIOF_CLK_ENABLE();
    __HAL_RCC_GPIOA_CLK_ENABLE();
    __HAL_RCC_GPIOB_CLK_ENABLE();

    // 출력 핀 초기 레벨 설정
    HAL_GPIO_WritePin(GPIOA, GPIO_PIN_0|GPIO_PIN_1|GPIO_PIN_4|GPIO_PIN_6
                            |GPIO_PIN_7|GPIO_PIN_10|GPIO_PIN_12, GPIO_PIN_RESET);
    HAL_GPIO_WritePin(GPIOC, GPIO_PIN_4|GPIO_PIN_5, GPIO_PIN_RESET);
    HAL_GPIO_WritePin(GPIOB, GPIO_PIN_0|GPIO_PIN_3|GPIO_PIN_6, GPIO_PIN_RESET);

    // 홀 센서 입력 핀 (PC0, PC1, PC6, PC10) - Pull-up
    GPIO_InitStruct.Pin = GPIO_PIN_0|GPIO_PIN_1|GPIO_PIN_6|GPIO_PIN_10;
    GPIO_InitStruct.Mode = GPIO_MODE_INPUT;
    GPIO_InitStruct.Pull = GPIO_PULLUP;
    HAL_GPIO_Init(GPIOC, &GPIO_InitStruct);

    // 세그먼트 및 부저 출력 핀 (PA)
    GPIO_InitStruct.Pin = GPIO_PIN_0|GPIO_PIN_1|GPIO_PIN_4|GPIO_PIN_6
                        |GPIO_PIN_7|GPIO_PIN_10|GPIO_PIN_12;
    GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
    GPIO_InitStruct.Pull = GPIO_NOPULL;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);

    // 키패드 Row 출력 핀 (PC4, PC5)
    GPIO_InitStruct.Pin = GPIO_PIN_4|GPIO_PIN_5;
    GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
    GPIO_InitStruct.Pull = GPIO_NOPULL;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(GPIOC, &GPIO_InitStruct);

    // 세그먼트 출력 핀 (PB0, PB3, PB6)
    GPIO_InitStruct.Pin = GPIO_PIN_0|GPIO_PIN_3|GPIO_PIN_6;
    GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
    GPIO_InitStruct.Pull = GPIO_NOPULL;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(GPIOB, &GPIO_InitStruct);

    // 키패드 Col 입력 핀 (PB10, PB4, PB5) - Pull-up
    GPIO_InitStruct.Pin = GPIO_PIN_10|GPIO_PIN_4|GPIO_PIN_5;
    GPIO_InitStruct.Mode = GPIO_MODE_INPUT;
    GPIO_InitStruct.Pull = GPIO_PULLUP;
    HAL_GPIO_Init(GPIOB, &GPIO_InitStruct);

    // 키패드 Col 입력 핀 (PA8, PA9) - Pull-up
    GPIO_InitStruct.Pin = GPIO_PIN_8|GPIO_PIN_9;
    GPIO_InitStruct.Mode = GPIO_MODE_INPUT;
    GPIO_InitStruct.Pull = GPIO_PULLUP;
    HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);
}

void Error_Handler(void)
{
    // 에러 발생 시 무한 루프 (디버깅용)
    __disable_irq();
    while (1) {}
}

#ifdef USE_FULL_ASSERT
void assert_failed(uint8_t *file, uint32_t line) {}
#endif
