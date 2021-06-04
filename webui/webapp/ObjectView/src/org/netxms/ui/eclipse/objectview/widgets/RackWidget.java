/**
 * NetXMS - open source network management system
 * Copyright (C) 2003-2021 Raden Solutions
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */
package org.netxms.ui.eclipse.objectview.widgets;

import java.util.ArrayList;
import java.util.HashSet;
import java.util.List;
import java.util.Set;
import java.util.UUID;
import org.eclipse.swt.SWT;
import org.eclipse.swt.events.DisposeEvent;
import org.eclipse.swt.events.DisposeListener;
import org.eclipse.swt.events.MouseEvent;
import org.eclipse.swt.events.MouseListener;
import org.eclipse.swt.events.MouseTrackListener;
import org.eclipse.swt.events.PaintEvent;
import org.eclipse.swt.events.PaintListener;
import org.eclipse.swt.graphics.Font;
import org.eclipse.swt.graphics.GC;
import org.eclipse.swt.graphics.Image;
import org.eclipse.swt.graphics.Point;
import org.eclipse.swt.graphics.Rectangle;
import org.eclipse.swt.widgets.Canvas;
import org.eclipse.swt.widgets.Composite;
import org.eclipse.ui.IWorkbenchPage;
import org.eclipse.ui.PartInitException;
import org.eclipse.ui.PlatformUI;
import org.netxms.base.NXCommon;
import org.netxms.client.constants.RackOrientation;
import org.netxms.client.objects.Chassis;
import org.netxms.client.objects.Rack;
import org.netxms.client.objects.configs.PassiveRackElement;
import org.netxms.client.objects.interfaces.HardwareEntity;
import org.netxms.ui.eclipse.console.resources.StatusDisplayInfo;
import org.netxms.ui.eclipse.console.resources.ThemeEngine;
import org.netxms.ui.eclipse.imagelibrary.shared.ImageProvider;
import org.netxms.ui.eclipse.imagelibrary.shared.ImageUpdateListener;
import org.netxms.ui.eclipse.objectview.Activator;
import org.netxms.ui.eclipse.objectview.views.ChassisView;
import org.netxms.ui.eclipse.objectview.views.PhysicalLinkView;
import org.netxms.ui.eclipse.objectview.widgets.helpers.ElementSelectionListener;
import org.netxms.ui.eclipse.tools.FontTools;
import org.netxms.ui.eclipse.tools.MessageDialogHelper;
import org.netxms.ui.eclipse.tools.WidgetHelper;

/**
 * Rack display widget
 */
public class RackWidget extends Canvas implements PaintListener, DisposeListener, ImageUpdateListener, MouseListener, MouseTrackListener
{
   private static final double UNIT_WH_RATIO = 10.85;
   private static final int BORDER_WIDTH_RATIO = 15;
   private static final int FULL_UNIT_WIDTH = 482;
   private static final int FULL_UNIT_HEIGHT = 45;
   private static final int MARGIN_HEIGHT = 10;
   private static final int MARGIN_WIDTH = 5;
   private static final int UNIT_NUMBER_WIDTH = 30;
   private static final int TITLE_HEIGHT = 20;
   private static final int MAX_BORDER_WIDTH = 30;
   private static final String[] FONT_NAMES = { "Segoe UI", "Liberation Sans", "DejaVu Sans", "Verdana", "Arial" }; //$NON-NLS-1$ //$NON-NLS-2$ //$NON-NLS-3$ //$NON-NLS-4$ //$NON-NLS-5$
   private static final String[] VIEW_LABELS = { "Front", "Back" };
   
   private static Image imageDefaultTop = null;
   private static Image imageDefaultMiddle;
   private static Image imageDefaultBottom;
   private static Image imageDefaultRear;
   private static Image imagePatchPanel;
   private static Image imageFillerPanel;
   private static Image imageOrganiserPanel;
   private static Object syncObject = new Object();

   /**
    * Create images required for rack drawing
    */
   private static void createImages()
   {
      synchronized(syncObject)
      {
         if (imageDefaultTop != null)
            return;

         imageDefaultTop = Activator.getImageDescriptor("icons/rack-default-top.png").createImage(); //$NON-NLS-1$
         imageDefaultMiddle = Activator.getImageDescriptor("icons/rack-default-middle.png").createImage(); //$NON-NLS-1$
         imageDefaultBottom = Activator.getImageDescriptor("icons/rack-default-bottom.png").createImage(); //$NON-NLS-1$
         imageDefaultRear = Activator.getImageDescriptor("icons/rack-default-rear.png").createImage(); //$NON-NLS-1$
         imagePatchPanel = Activator.getImageDescriptor("icons/rack-patch-panel.png").createImage();
         imageOrganiserPanel = Activator.getImageDescriptor("icons/rack-filler-panel.png").createImage(); // FIXME use actual organiser
                                                                                                          // panel image once
                                                                                                          // available!
         imageFillerPanel = Activator.getImageDescriptor("icons/rack-filler-panel.png").createImage();
      }
   }

   private Rack rack;
   private Font[] labelFonts;
   private Font[] titleFonts;
   private List<ObjectImage> objects = new ArrayList<ObjectImage>();
   private Object selectedObject = null;
   private Set<ElementSelectionListener> selectionListeners = new HashSet<ElementSelectionListener>(0);
   private Object tooltipObject = null;
   private ObjectPopupDialog tooltipDialog = null;
   private RackOrientation view;
   
   /**
    * @param parent
    * @param style
    */
   public RackWidget(Composite parent, int style, Rack rack, RackOrientation view)
   {
      super(parent, style | SWT.DOUBLE_BUFFERED);
      this.rack = rack;
      this.view = (view == RackOrientation.FILL) ? RackOrientation.FRONT : view;
      
      setBackground(ThemeEngine.getBackgroundColor("Rack"));
      
      labelFonts = FontTools.getFonts(FONT_NAMES, 6, SWT.NORMAL, 16);
      titleFonts = FontTools.getFonts(FONT_NAMES, 6, SWT.BOLD, 16);
      
      addPaintListener(this);
      addMouseListener(this);
      WidgetHelper.attachMouseTrackListener(this, this);
      addDisposeListener(this);
      ImageProvider.getInstance().addUpdateListener(this);
      createImages();
   }
   
   /**
    * @return the currentObject
    */
   public Object getCurrentObject()
   {
      return selectedObject;
   }

   /**
    * @see org.eclipse.swt.events.PaintListener#paintControl(org.eclipse.swt.events.PaintEvent)
    */
   @Override
   public void paintControl(PaintEvent e)
   {
      final GC gc = e.gc;

      gc.setAntialias(SWT.ON);
      //gc.setInterpolation(SWT.HIGH);

      // Calculate bounding box for rack picture
      Rectangle rect = getClientArea();
      rect.x += MARGIN_WIDTH + UNIT_NUMBER_WIDTH;
      rect.y += MARGIN_HEIGHT + MARGIN_HEIGHT / 2 + TITLE_HEIGHT;
      rect.height -= MARGIN_HEIGHT * 2 + MARGIN_HEIGHT / 2 + TITLE_HEIGHT;

      // Estimated unit width/height and calculate border width
      double unitHeight = (double)rect.height / (double)rack.getHeight();
      int unitWidth = (int)(unitHeight * UNIT_WH_RATIO);
      int borderWidth = unitWidth / BORDER_WIDTH_RATIO;
      if (borderWidth < 3)
         borderWidth = 3;
      else if (borderWidth > MAX_BORDER_WIDTH)
         borderWidth = MAX_BORDER_WIDTH;
      rect.height -= borderWidth + borderWidth / 2;
      rect.y += borderWidth / 2;

      // precise unit width and height taking borders into consideration
      unitHeight = (double)(rect.height - ((borderWidth + 1) / 2) * 2) / (double)rack.getHeight();
      unitWidth = (int)(unitHeight * UNIT_WH_RATIO);
      rect.width = unitWidth + borderWidth * 2;
      rect.x += borderWidth / 2;

      // Title
      gc.setFont(WidgetHelper.getBestFittingFont(gc, titleFonts, VIEW_LABELS[0], rect.width, TITLE_HEIGHT)); //$NON-NLS-1$
      Point titleSize = gc.textExtent(VIEW_LABELS[view.getValue() - 1]);
      gc.drawText(VIEW_LABELS[view.getValue() - 1], (rect.width / 2 - titleSize.x / 2) + UNIT_NUMBER_WIDTH + MARGIN_WIDTH, rect.y - TITLE_HEIGHT - MARGIN_HEIGHT / 2 - borderWidth / 2);

      // Rack itself
      gc.setBackground(ThemeEngine.getBackgroundColor("Rack.EmptySpace"));
      gc.fillRoundRectangle(rect.x, rect.y, rect.width, rect.height, 3, 3);
      gc.setLineWidth(borderWidth);
      gc.setForeground(ThemeEngine.getForegroundColor("Rack.Border"));
      gc.drawRoundRectangle(rect.x, rect.y, rect.width, rect.height, 3, 3);

      // Rack bottom
      gc.setBackground(ThemeEngine.getBackgroundColor("Rack.Border"));
      gc.fillRectangle(rect.x + borderWidth * 2 - (borderWidth + 1) / 2, rect.y + rect.height, borderWidth * 2, (int)(borderWidth * 1.5));
      gc.fillRectangle(rect.x + rect.width - borderWidth * 3 - (borderWidth + 1) / 2, rect.y + rect.height, borderWidth * 2, (int)(borderWidth * 1.5));

      // Draw unit numbers
      int[] unitBaselines = new int[rack.getHeight() + 1];
      gc.setFont(WidgetHelper.getBestFittingFont(gc, labelFonts, "00", UNIT_NUMBER_WIDTH, (int)unitHeight - 2)); //$NON-NLS-1$
      gc.setForeground(ThemeEngine.getForegroundColor("Rack"));
      gc.setBackground(ThemeEngine.getBackgroundColor("Rack"));
      gc.setLineWidth(1);
      double dy = rack.isTopBottomNumbering() ? rect.y + unitHeight + (borderWidth + 1) / 2 : rect.y + rect.height - (borderWidth + 1) / 2;
      if (rack.isTopBottomNumbering())
         unitBaselines[0] = (int)(dy - unitHeight);
      for(int u = 1; u <= rack.getHeight(); u++)
      {
         int y = (int)dy;
         gc.drawLine(MARGIN_WIDTH, y, UNIT_NUMBER_WIDTH, y);
         String label = Integer.toString(u);
         Point textExtent = gc.textExtent(label);
         gc.drawText(label, UNIT_NUMBER_WIDTH - textExtent.x, y - (int)unitHeight / 2 - textExtent.y / 2);
         if (rack.isTopBottomNumbering())
         {
            unitBaselines[u] = y;
            dy += unitHeight;
         }
         else
         {
            unitBaselines[u - 1] = y;
            dy -= unitHeight;
         }
      }
      if (!rack.isTopBottomNumbering())
         unitBaselines[rack.getHeight()] = (int)dy;
      
      // Draw attributes
      objects.clear();
      List<PassiveRackElement> attributes = rack.getPassiveElements();
      for(PassiveRackElement c : attributes)
      {
         if ((c.getPosition() < 1) || (c.getPosition() > rack.getHeight()) ||
            (c.getOrientation() != view) && (c.getOrientation() != RackOrientation.FILL))
            continue;
         
         int topLine, bottomLine;
         if (rack.isTopBottomNumbering())
         {
            bottomLine = unitBaselines[c.getPosition()]; // lower border
            topLine = unitBaselines[c.getPosition() - 1];   // upper border
         }
         else
         {
            bottomLine = unitBaselines[c.getPosition() - 1]; // lower border
            topLine = unitBaselines[c.getPosition()];   // upper border
         }
         final Rectangle unitRect = new Rectangle(rect.x + (borderWidth + 1) / 2, topLine + 1, rect.width - borderWidth, bottomLine - topLine);

         if ((unitRect.width <= 0) || (unitRect.height <= 0))
            break;

         objects.add(new ObjectImage(c, unitRect));
         Image image;
         switch(c.getType())
         {
            case FILLER_PANEL:
               image = imageFillerPanel;
               break;
            case PATCH_PANEL:
               image = imagePatchPanel;
               break;
            case ORGANISER:
               image = imageOrganiserPanel;
               break;
            default:
               continue;
         }

         Rectangle r = image.getBounds();
         gc.drawImage(image, 0, 0, r.width, r.height, unitRect.x, unitRect.y, unitRect.width, unitRect.height);
      }
      
      // Draw units
      List<HardwareEntity> units = rack.getUnits();
      for(HardwareEntity n : units)
      {
         if ((n.getRackPosition() < 1) || (n.getRackPosition() > rack.getHeight()) || 
             (rack.isTopBottomNumbering() && (n.getRackPosition() + n.getRackHeight() > rack.getHeight() + 1)) ||
             (!rack.isTopBottomNumbering() && (n.getRackPosition() - n.getRackHeight() < 0)) || 
             ((n.getRackOrientation() != view) && (n.getRackOrientation() != RackOrientation.FILL)))
            continue;
         
         int topLine, bottomLine;
         if (rack.isTopBottomNumbering())
         {
            bottomLine = unitBaselines[n.getRackPosition() + n.getRackHeight() - 1]; // lower border
            topLine = unitBaselines[n.getRackPosition() - 1];   // upper border
         }
         else
         {
            bottomLine = unitBaselines[n.getRackPosition() - n.getRackHeight()]; // lower border
            topLine = unitBaselines[n.getRackPosition()];   // upper border
         }
         final Rectangle unitRect = new Rectangle(rect.x + (borderWidth + 1) / 2, topLine + 1, rect.width - borderWidth, bottomLine - topLine);

         if ((unitRect.width <= 0) || (unitRect.height <= 0))
            break;

         objects.add(new ObjectImage(n, unitRect));

         // draw status indicator
         gc.setBackground(StatusDisplayInfo.getStatusColor(n.getStatus()));
         gc.fillRectangle(unitRect.x - borderWidth + borderWidth / 4 + 1, unitRect.y + 1, borderWidth / 2 - 1, Math.min(borderWidth, (int)unitHeight - 2));

         if(n instanceof Chassis)
         {
            ChassisWidget.drawChassis(gc, unitRect, (Chassis)n, view, null);
         }
         else if ((n.getRearRackImage() != null) && !n.getRearRackImage().equals(NXCommon.EMPTY_GUID) && view == RackOrientation.REAR)
         {
            Image image = ImageProvider.getInstance().getImage(n.getRearRackImage());
            Rectangle r = image.getBounds();
            gc.drawImage(image, 0, 0, r.width, r.height, unitRect.x, unitRect.y, unitRect.width, unitRect.height);
         }
         else if ((n.getFrontRackImage() != null) && !n.getFrontRackImage().equals(NXCommon.EMPTY_GUID) && view == RackOrientation.FRONT)
         {
            Image image = ImageProvider.getInstance().getImage(n.getFrontRackImage());
            Rectangle r = image.getBounds();
            gc.drawImage(image, 0, 0, r.width, r.height, unitRect.x, unitRect.y, unitRect.width, unitRect.height);
         }         
         else // Draw default representation
         {
            Image imageTop = (view == RackOrientation.REAR && n.getRackOrientation() == RackOrientation.FILL) ? imageDefaultRear : imageDefaultTop;
            
            Rectangle r = imageTop.getBounds();
            if (n.getRackHeight() == 1)
            {
                  gc.drawImage(imageTop, 0, 0, r.width, r.height, unitRect.x, unitRect.y, unitRect.width, unitRect.height);
            }
            else
            {
               Image imageMiddle = (view == RackOrientation.REAR && n.getRackOrientation() == RackOrientation.FILL) ? imageDefaultRear : imageDefaultMiddle;
               Image imageBottom = (view == RackOrientation.REAR && n.getRackOrientation() == RackOrientation.FILL) ? imageDefaultRear : imageDefaultBottom;
               if (rack.isTopBottomNumbering())
               {
                  unitRect.height = unitBaselines[n.getRackPosition()] - topLine;
                  gc.drawImage(imageTop, 0, 0, r.width, r.height, unitRect.x, unitRect.y, unitRect.width, unitRect.height);
                  
                  r = imageMiddle.getBounds();
                  int u = n.getRackPosition() + 1;
                  for(int i = 1; i < n.getRackHeight() - 1; i++, u++)
                  {
                     unitRect.y = unitBaselines[u - 1];
                     unitRect.height = unitBaselines[u] - unitRect.y;
                     gc.drawImage(imageMiddle, 0, 0, r.width, r.height, unitRect.x, unitRect.y, unitRect.width, unitRect.height);
                  }
                  
                  r = imageBottom.getBounds();
                  unitRect.y = unitBaselines[u - 1];
                  unitRect.height = unitBaselines[u] - unitRect.y;
                  gc.drawImage(imageBottom, 0, 0, r.width, r.height, unitRect.x, unitRect.y, unitRect.width, unitRect.height);
               }
               else
               {
                  unitRect.height = unitBaselines[n.getRackPosition() - 1] - topLine;
                  gc.drawImage(imageTop, 0, 0, r.width, r.height, unitRect.x, unitRect.y, unitRect.width, unitRect.height);
   
                  r = imageMiddle.getBounds();
                  int u = n.getRackPosition() - 1;
                  for(int i = 1; i < n.getRackHeight() - 1; i++, u--)
                  {
                     unitRect.y = unitBaselines[u];
                     unitRect.height = unitBaselines[u - 1] - unitRect.y;
                     gc.drawImage(imageMiddle, 0, 0, r.width, r.height, unitRect.x, unitRect.y, unitRect.width, unitRect.height);
                  }
                  
                  r = imageBottom.getBounds();
                  unitRect.y = unitBaselines[u];
                  unitRect.height = unitBaselines[u - 1] - unitRect.y;
                  gc.drawImage(imageBottom, 0, 0, r.width, r.height, unitRect.x, unitRect.y, unitRect.width, unitRect.height);
               }
            }
         }
      }
   }

   /**
    * @see org.eclipse.swt.widgets.Composite#computeSize(int, int, boolean)
    */
   @Override
   public Point computeSize(int wHint, int hHint, boolean changed)
   {
      if (hHint == SWT.DEFAULT && wHint == SWT.DEFAULT)
         return new Point(10, 10);

      if (hHint == SWT.DEFAULT)
      {
         int borderWidth = FULL_UNIT_WIDTH / BORDER_WIDTH_RATIO;
         return new Point(FULL_UNIT_WIDTH + MARGIN_WIDTH * 2 + UNIT_NUMBER_WIDTH + borderWidth * 2 + borderWidth / 2,
               rack.getHeight() * FULL_UNIT_HEIGHT + MARGIN_HEIGHT * 2 + borderWidth * 2 + MARGIN_HEIGHT / 2 + TITLE_HEIGHT);
      }

      double unitHeight = (double)hHint / (double)rack.getHeight();
      int unitWidth = (int)(unitHeight * UNIT_WH_RATIO);
      int borderWidth = unitWidth / BORDER_WIDTH_RATIO;
      if (borderWidth < 3)
         borderWidth = 3;
      else if (borderWidth > MAX_BORDER_WIDTH)
         borderWidth = MAX_BORDER_WIDTH;

      unitWidth = (int)((double)(hHint - ((borderWidth + 1) / 2) * 2 - MARGIN_HEIGHT * 2) / (double)rack.getHeight() * UNIT_WH_RATIO);
      return new Point(unitWidth + MARGIN_WIDTH * 2 + UNIT_NUMBER_WIDTH + borderWidth * 2 + borderWidth / 2, hHint);
   }

   /**
    * @see org.eclipse.swt.events.DisposeListener#widgetDisposed(org.eclipse.swt.events.DisposeEvent)
    */
   @Override
   public void widgetDisposed(DisposeEvent e)
   {      
      ImageProvider.getInstance().removeUpdateListener(this);
   }

   /**
    * @see org.netxms.ui.eclipse.imagelibrary.shared.ImageUpdateListener#imageUpdated(java.util.UUID)
    */
   @Override
   public void imageUpdated(UUID guid)
   {
      boolean found = false;
      List<HardwareEntity> units = rack.getUnits();
      for(HardwareEntity e : units)
      {
         if (guid.equals(e.getFrontRackImage()) || guid.equals(e.getRearRackImage()))
         {
            found = true;
            break;
         }
      }
      if (found)
      {
         getDisplay().asyncExec(new Runnable() {
            @Override
            public void run()
            {
               if (!isDisposed())
                  redraw();
            }
         });
      }
   }

   /**
    * Get object at given point
    * 
    * @param p
    * @return
    */
   private Object getObjectAtPoint(Point p)
   {
      for(ObjectImage i : objects)
         if (i.contains(p))
         {
            return i.getObject();
         }
      return null;
   }

   /**
    * @see org.eclipse.swt.events.MouseListener#mouseDoubleClick(org.eclipse.swt.events.MouseEvent)
    */
   @Override
   public void mouseDoubleClick(MouseEvent e)
   {
      if (selectedObject instanceof PassiveRackElement)
      {
         try 
         {
            PlatformUI.getWorkbench().getActiveWorkbenchWindow().getActivePage().showView(PhysicalLinkView.ID, String.valueOf(rack.getObjectId()) + "&" + String.valueOf(((PassiveRackElement)selectedObject).getId()), IWorkbenchPage.VIEW_ACTIVATE);            
         } 
         catch (PartInitException ex) 
         {
            MessageDialogHelper.openError(getShell(), "Error openning Physical link view", String.format("Cannot open Physical link view: %s", ex.getLocalizedMessage()));
         }
      }
      else if (selectedObject instanceof Chassis)
      {
         try 
         {
            PlatformUI.getWorkbench().getActiveWorkbenchWindow().getActivePage().showView(ChassisView.ID, String.valueOf(((Chassis)selectedObject).getObjectId()), IWorkbenchPage.VIEW_ACTIVATE);            
         } 
         catch (PartInitException ex) 
         {
            MessageDialogHelper.openError(getShell(), "Error openning Physical link view", String.format("Cannot open Physical link view: %s", ex.getLocalizedMessage()));
         }
      }
   }

   /**
    * @see org.eclipse.swt.events.MouseListener#mouseDown(org.eclipse.swt.events.MouseEvent)
    */
   @Override
   public void mouseDown(MouseEvent e)
   {  
      if ((tooltipDialog != null) && (tooltipDialog.getShell() != null) && !tooltipDialog.getShell().isDisposed() && (e.display.getActiveShell() != tooltipDialog.getShell()))
      {
         tooltipDialog.close();
         tooltipDialog = null;
      }
      tooltipObject = null;
      setCurrentObject(getObjectAtPoint(new Point(e.x, e.y)));
   }

   /**
    * @see org.eclipse.swt.events.MouseListener#mouseUp(org.eclipse.swt.events.MouseEvent)
    */
   @Override
   public void mouseUp(MouseEvent e)
   {
   }
   
   /**
    * @see org.eclipse.swt.events.MouseTrackListener#mouseEnter(org.eclipse.swt.events.MouseEvent)
    */
   @Override
   public void mouseEnter(MouseEvent e)
   {
   }

   /**
    * @see org.eclipse.swt.events.MouseTrackListener#mouseExit(org.eclipse.swt.events.MouseEvent)
    */
   @Override
   public void mouseExit(MouseEvent e)
   {
      if ((tooltipDialog != null) && (tooltipDialog.getShell() != null) && !tooltipDialog.getShell().isDisposed() && (e.display.getActiveShell() != tooltipDialog.getShell()))
      {
         tooltipDialog.close();
         tooltipDialog = null;
      }
      tooltipObject = null;
   }

   /**
    * @see org.eclipse.swt.events.MouseTrackListener#mouseHover(org.eclipse.swt.events.MouseEvent)
    */
   @Override
   public void mouseHover(MouseEvent e)
   {
      Object object = getObjectAtPoint(new Point(e.x, e.y));
      if ((object != null) && ((object != tooltipObject) || (tooltipDialog == null) || (tooltipDialog.getShell() == null) || tooltipDialog.getShell().isDisposed()))
      {
         if ((tooltipDialog != null) && (tooltipDialog.getShell() != null) && !tooltipDialog.getShell().isDisposed())
            tooltipDialog.close();
      
         tooltipObject = object;
         tooltipDialog = new ObjectPopupDialog(getShell(), object, toDisplay(e.x, e.y));
         tooltipDialog.open();
      }
      else if ((object == null) && (tooltipDialog != null) && (tooltipDialog.getShell() != null) && !tooltipDialog.getShell().isDisposed())
      {
         tooltipDialog.close();
         tooltipDialog = null;
      }
   }

   /**
    * Add selection listener
    * 
    * @param listener
    */
   public void addSelectionListener(ElementSelectionListener listener)
   {
      selectionListeners.add(listener);
   }
   
   /**
    * Remove selection listener
    * 
    * @param listener
    */
   public void removeSelectionListener(ElementSelectionListener listener)
   {
      selectionListeners.remove(listener);
   }
   
   /**
    * Set current selection
    * 
    * @param o
    */
   private void setCurrentObject(Object o)
   {
      selectedObject = o;
      for(ElementSelectionListener l : selectionListeners)
         l.objectSelected(selectedObject);
   }
   
   /**
    * Object image information
    */
   private class ObjectImage
   {
      private Object object;
      private Rectangle rect;

      public ObjectImage(Object object, Rectangle rect)
      {
         this.object = object;
         this.rect = new Rectangle(rect.x, rect.y, rect.width, rect.height);
      }
      
      public boolean contains(Point p)
      {
         return rect.contains(p);
      }
      
      public Object getObject()
      {
         return object;
      }
   }

   /**
    * @return the imageDefaultTop
    */
   public static Image getImageDefaultTop()
   {
      createImages();
      return imageDefaultTop;
   }

   /**
    * @return the imageDefaultMiddle
    */
   public static Image getImageDefaultMiddle()
   {
      createImages();
      return imageDefaultMiddle;
   }

   /**
    * @return the imageDefaultBottom
    */
   public static Image getImageDefaultBottom()
   {
      createImages();
      return imageDefaultBottom;
   }

   /**
    * @return the imageDefaultRear
    */
   public static Image getImageDefaultRear()
   {
      createImages();
      return imageDefaultRear;
   }
}
