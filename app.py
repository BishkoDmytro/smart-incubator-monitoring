from flask import Flask, request, jsonify, render_template, send_file,request, redirect, url_for, render_template, flash
from flask_migrate import Migrate
from flask_sqlalchemy import SQLAlchemy
from flask_admin import Admin, BaseView, expose
from flask_admin import BaseView, expose
from flask_wtf import FlaskForm
from wtforms import DateField, IntegerField, SubmitField
from wtforms.validators import DataRequired
from fpdf import FPDF
from datetime import datetime
import os
import requests
import threading
import time
from collections import defaultdict
import json
import atexit, pytz
from pytz import timezone
from zoneinfo import ZoneInfo


from datetime import datetime
app = Flask(__name__)
app.config['SECRET_KEY'] = os.environ.get('SECRET_KEY', 'your-default-secret-key')
# Конфігурація
app.config['SQLALCHEMY_DATABASE_URI'] = 'postgresql://bishko:Yvo4uLkTb6VAAej88Y1BxRgTypYKw0UF@dpg-csua2ut2ng1s73cf7q4g-a/data_mkwp'
app.config['SQLALCHEMY_TRACK_MODIFICATIONS'] = False
app.config['SQLALCHEMY_ENGINE_OPTIONS'] = {
    'pool_pre_ping': True,
    'pool_recycle': 3600,
    'pool_size': 10,
    'pool_timeout': 60,
}

db = SQLAlchemy(app)
migrate = Migrate(app, db)

# Налаштування локального часу
LOCAL_TIMEZONE = pytz.timezone("Europe/Kyiv")  # Встановіть ваш часовий пояс
# Файл для збереження останніх даних
LATEST_DATA_FILE = "latest_data.json"

class SensorData(db.Model):
    id = db.Column(db.Integer, primary_key=True)
    sensor_type = db.Column(db.String(50))
    temperature = db.Column(db.Float)
    humidity = db.Column(db.Float)
    workshop = db.Column(db.Integer)
    incubator = db.Column(db.Integer)
    camera = db.Column(db.Integer)
    timestamp = db.Column(db.DateTime, default=lambda: datetime.now(pytz.utc))  # Зберігаємо в UTC


def load_latest_data():
    """Завантажити останні дані з файлу."""
    if os.path.exists(LATEST_DATA_FILE):
        with open(LATEST_DATA_FILE, 'r') as file:
            try:
                return json.load(file)
            except json.JSONDecodeError:
                return {}
    return {}

def save_latest_data(data):
    """Зберегти останні дані у файл."""
    with open(LATEST_DATA_FILE, 'w') as file:
        json.dump(data, file)

# Завантаження останніх даних при запуску
latest_data = load_latest_data()

# Реєструємо функцію для збереження даних при завершенні програми
@atexit.register
def save_latest_data_on_exit():
    save_latest_data(latest_data)

@app.route('/')
def home():
    return render_template('index.html')

@app.route('/workshop/<int:workshop_id>')
def workshop(workshop_id):
    if workshop_id == 1:
        return render_template('workshop1.html')
    elif workshop_id == 2:
        return render_template('workshop2.html')
    else:
        return "Error: Workshop not found.", 404

@app.route('/incubator/<int:workshop_id>/<int:incubator_id>')
def incubator(workshop_id, incubator_id):
    return render_template('incubator.html', workshop_id=workshop_id, incubator_id=incubator_id)

@app.route('/camera/<int:workshop_id>/<int:incubator_id>/<int:camera_id>')
def camera(workshop_id, incubator_id, camera_id):
    key = f"{workshop_id}_{incubator_id}_{camera_id}"
    if key in latest_data:
        temp_data = latest_data[key]
        return render_template(
            'camera.html',
            workshop_id=workshop_id,
            incubator_id=incubator_id,
            camera_id=camera_id,
            temperature=temp_data['temperature'],
            humidity=temp_data['humidity']
        )
    else:
        return render_template('error.html', message="No sensor data available for this camera.")

from datetime import datetime, timedelta
from pytz import timezone

@app.route('/camera/<int:workshop>/<int:incubator>/<int:camera>/data', methods=['GET', 'POST'])
def get_camera_data(workshop, incubator, camera):
    try:
        selected_date = None
        data = []

        # Обробка дати для POST або GET-запиту
        if request.method == 'POST':
            selected_date = request.form.get('date')
        elif request.method == 'GET':
            selected_date = request.args.get('date')

        # Якщо вибрана дата передана
        if selected_date:
            # Конвертуємо обрану дату у формат datetime із часовою зоною Kyiv
            try:
                # Парсимо дату з формату YYYY-MM-DD
                start_date = datetime.strptime(selected_date, '%Y-%m-%d')
                start_date = timezone("Europe/Kyiv").localize(start_date)  # Локалізуємо для часового поясу Kyiv
                end_date = start_date + timedelta(days=1)  # Наступний день для фільтрації

            except ValueError:
                # Якщо формат дати неправильний, відображаємо помилку
                return render_template(
                    'error.html',
                    message="Invalid date format. Please use YYYY-MM-DD."
                )

            # Фільтрація даних за обраною датою
            data = SensorData.query.filter(
                SensorData.workshop == workshop,
                SensorData.incubator == incubator,
                SensorData.camera == camera,
                SensorData.timestamp >= start_date,
                SensorData.timestamp < end_date
            ).all()

        # Якщо дата не вибрана, повертаємо всі дані (опціонально)
        else:
            data = SensorData.query.filter_by(
                workshop=workshop,
                incubator=incubator,
                camera=camera
            ).all()

        # Перетворюємо час в Europe/Kyiv
        kyiv_tz = timezone("Europe/Kyiv")
        for record in data:
            record.timestamp = record.timestamp.astimezone(kyiv_tz)

        return render_template(
            'camera_data.html',
            data=data,
            workshop=workshop,
            incubator=incubator,
            camera=camera,
            selected_date=selected_date
        )

    except Exception as e:
        return render_template(
            'error.html',
            message=f"An error occurred: {str(e)}"
        )

@app.route('/send_data', methods=['POST'])
def send_data():
    try:
        sensor_data = request.json
        if not isinstance(sensor_data, list):
            return "Error: Expected a list of sensor data.", 400

        for entry in sensor_data:
            workshop = entry.get("workshop")
            incubator = entry.get("incubator")
            camera = entry.get("camera")
            temperature = entry.get("temperature")
            humidity = entry.get("humidity")
            save_to_database = entry.get("save_to_database", False)

            if None in (workshop, incubator, camera, temperature, humidity):
                return "Error: Missing required fields.", 400

            # Зберігаємо останні тимчасові дані
            key = f"{workshop}_{incubator}_{camera}"
            latest_data[key] = {
                "temperature": temperature,
                "humidity": humidity,
                "timestamp": datetime.now(pytz.utc).isoformat()
            }

            # Зберігаємо в БД, якщо потрібно
            if save_to_database:
                new_data = SensorData(
                    sensor_type=entry.get("sensor_type", "Unknown"),
                    temperature=temperature,
                    humidity=humidity,
                    workshop=workshop,
                    incubator=incubator,
                    camera=camera,
                    timestamp=datetime.now(LOCAL_TIMEZONE).astimezone(pytz.utc)  # Локальний час, перетворений в UTC
                )
                db.session.add(new_data)

        # Зберігаємо дані у файл
        save_latest_data(latest_data)

        db.session.commit()
        return jsonify({"message": "Data received successfully"}), 200
    except Exception as e:
        return jsonify({"error": str(e)}), 500

@app.route('/ping', methods=['GET'])
def ping():
    return "Server is awake", 200

def keep_server_awake():
    while True:
        time.sleep(840)  # 14 хвилин у секундах
        try:
            requests.get("https://incubator-w0ut.onrender.com/ping")  # Замініть на вашу адресу
        except Exception as e:
            print(f"Error keeping server awake: {e}")

# Запуск фонової функції
threading.Thread(target=keep_server_awake, daemon=True).start()


# Форма для діапазону дат
class DateRangeForm(FlaskForm):
    start_date = DateField('Початкова дата', validators=[DataRequired()])
    end_date = DateField('Кінцева дата', validators=[DataRequired()])
    incubator = IntegerField('Інкубатор', validators=[DataRequired()])
    camera = IntegerField('Камера', validators=[DataRequired()])


ADMIN_PASSWORD = '1234'  # замінити на реальний пароль


# Додаємо сторінку для введення пароля перед доступом
@app.route('/admin/login', methods=['GET', 'POST'])
def admin_login():
    if request.method == 'POST':
        password = request.form.get('password')
        if password == ADMIN_PASSWORD:
            return redirect(url_for('admin_data.index'))  # Перехід до адміністративної панелі
        else:
            flash('Невірний пароль', 'danger')
            return render_template('admin_login.html')  # Показуємо форму з повідомленням про помилку
    return render_template('admin_login.html')
# Ваш клас для обробки адміністрування
class MyAdminView(BaseView):
    @expose('/', methods=['GET', 'POST'])
    def index(self):
        # Якщо форма з паролем передана
        if 'password' in request.form:
            password = request.form['password']
            if password != ADMIN_PASSWORD:
                flash('Невірний пароль', 'danger')
                return self.render('admin_login.html')

        form = DateRangeForm()
        if form.validate_on_submit():
            try:
                if 'delete' in request.form:
                    self.delete_data(
                        form.start_date.data,
                        form.end_date.data,
                        form.incubator.data,
                        form.camera.data
                    )
                    success_message = "Дані успішно видалено."
                elif 'export' in request.form:
                    pdf_path = self.export_data(
                        form.start_date.data,
                        form.end_date.data,
                        form.incubator.data,
                        form.camera.data
                    )
                    success_message = f'PDF файл згенеровано: <a href="/{pdf_path}" target="_blank">Завантажити</a>'
                return self.render('admin.html', form=form, success=success_message)
            except ValueError as e:
                return self.render('admin.html', form=form, error=str(e))
        return self.render('admin.html', form=form)

    def delete_data(self, start_date, end_date, incubator, camera):
        SensorData.query.filter(
            SensorData.timestamp.between(start_date, end_date),
            SensorData.incubator == incubator,
            SensorData.camera == camera
        ).delete()
        db.session.commit()

    def export_data(self, start_date, end_date, incubator, camera):
        """Експортування даних у PDF з розділенням по днях"""
        end_date = datetime.combine(end_date, datetime.max.time())
        # Витягуємо дані за вказаний часовий проміжок
        data = SensorData.query.filter(
            SensorData.timestamp.between(start_date, end_date),
            SensorData.incubator == incubator,
            SensorData.camera == camera
        ).order_by(SensorData.timestamp).all()

        if not data:
            raise ValueError("Дані не знайдено для експорту.")

        # Створення PDF
        pdf = FPDF()
        pdf.add_page()

        # Додавання шрифтів
        font_path = os.path.join("static", "fonts", "DejaVuSans.ttf")
        bold_font_path = os.path.join("static", "fonts", "DejaVuSans-Bold.ttf")
        pdf.add_font("DejaVu", "", font_path, uni=True)
        pdf.add_font("DejaVu", "B", bold_font_path, uni=True)

        # Заголовок PDF
        pdf.set_font("DejaVu", "B", 14)
        pdf.cell(200, 10, txt="Експорт даних", ln=True, align="C")

        # Групування даних по днях
        grouped_data = defaultdict(list)
        for record in data:
            day = record.timestamp.date()
            grouped_data[day].append(record)

        # Створення таблиць для кожного дня
        for day, records in grouped_data.items():
            pdf.ln(10)  # Додати відступ перед новим розділом
            pdf.set_font("DejaVu", "B", 12)
            pdf.cell(200, 10, txt=f"Дата: {day.strftime('%Y-%m-%d')}", ln=True)

            # Заголовок таблиці
            pdf.set_font("DejaVu", size=10)
            pdf.cell(40, 10, "Sensor", border=1)
            pdf.cell(30, 10, "Temp", border=1)
            pdf.cell(30, 10, "Humidity", border=1)
            pdf.cell(40, 10, "Timestamp", border=1)
            pdf.ln()

            # Дані таблиці
            for record in records:
                pdf.cell(40, 10, record.sensor_type, border=1)
                pdf.cell(30, 10, f"{record.temperature}", border=1)
                pdf.cell(30, 10, f"{record.humidity}", border=1)
                pdf.cell(40, 10, record.timestamp.strftime('%H:%M:%S'), border=1)
                pdf.ln()

        # Збереження PDF
        pdf_path = 'static/data.pdf'
        os.makedirs(os.path.dirname(pdf_path), exist_ok=True)
        pdf.output(pdf_path)
        return pdf_path


# Ініціалізація адмін-панелі
admin = Admin(app, name='Admin Panel', template_mode='bootstrap3')

# Here we explicitly set the name and route for the admin view
admin.add_view(MyAdminView(name='Керування даними', endpoint='admin_data'))




if __name__ == '__main__':
    with app.app_context():
        db.create_all()  # Створення таблиць у базі даних
    app.run(debug=True, host='0.0.0.0')
